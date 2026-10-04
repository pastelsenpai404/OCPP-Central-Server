"""Isolated mock and real WebSocket tests. Never contacts an existing CSMS or DB."""
import base64
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from websockets.sync.server import serve
from jsonschema.validators import validator_for

ROOT=Path(__file__).resolve().parents[2]
SCHEMAS=Path(os.environ.get('SIM_TEST_SCHEMAS',str(ROOT.parents[1]/'ocpp_csms/schemas')))

def freeport():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]

def validate(protocol,name,body):
    schema=SCHEMAS/('2.0.1/'+name+'Request.json' if protocol=='ocpp2.0.1' else name+'.json')
    if not schema.exists():raise AssertionError('Missing OCPP test schema: '+str(schema))
    document=json.loads(schema.read_text());validator_for(document)(document).validate(body)

def wait(test,timeout=12):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        result=test()
        if result:return result
        time.sleep(.15)
    raise AssertionError('State did not reach expected condition')

def suite(binary):
    token=secrets.token_hex(32);station_secret=secrets.token_hex(32)
    port=freeport();wsport=freeport();host=f'127.0.0.1:{port}';origin='http://'+host
    errors=[];frames=[];commands=[];ack_started=threading.Event();connections=[]
    delay_start=threading.Event();start_received=threading.Event()
    def charger(socket):
        try:
            protocol=socket.subprotocol
            assert protocol in ('ocpp1.6','ocpp2.0.1')
            assert socket.request.headers.get('Origin') is None
            assert socket.request.headers['Authorization']=='Basic '+base64.b64encode(('SIMREAL:'+station_secret).encode()).decode()
            connections.append(protocol)
            for text in socket:
                frame=json.loads(text)
                if frame[0] in (3,4):commands.append(frame);continue
                _,uid,name,body=frame;validate(protocol,name,body);frames.append((protocol,name,body))
                response={}
                if name=='BootNotification':response={'status':'Accepted','currentTime':'2026-10-02T00:00:00Z','interval':30}
                if name=='Heartbeat':response={'currentTime':'2026-10-02T00:00:00Z'}
                if name=='Authorize':response['idTagInfo' if protocol=='ocpp1.6' else 'idTokenInfo']={'status':'Accepted'}
                if name=='StartTransaction':response={'transactionId':42,'idTagInfo':{'status':'Accepted'}}
                if name=='StartTransaction' or(name=='TransactionEvent' and body['eventType']=='Started'):
                    if delay_start.is_set():
                        delay_start.clear();start_received.set();time.sleep(.8)
                socket.send(json.dumps([3,uid,response]))
                if name=='StartTransaction' or(name=='TransactionEvent' and body['eventType']=='Started'):
                    ack_started.set()
                    socket.send(json.dumps([2,'unsupported','UpdateFirmware',{}]))
                    socket.send(json.dumps([2,'limit','SetChargingProfile',{'connectorId':1,'csChargingProfiles':{'chargingProfileId':1,'stackLevel':0,'chargingProfilePurpose':'TxDefaultProfile','chargingProfileKind':'Absolute','chargingSchedule':{'chargingRateUnit':'W','chargingSchedulePeriod':[{'startPeriod':0,'limit':1000}]}}}]) if protocol=='ocpp1.6' else json.dumps([2,'limit','SetChargingProfile',{'evseId':1,'chargingProfile':{'id':1,'stackLevel':0,'chargingProfilePurpose':'TxDefaultProfile','chargingProfileKind':'Absolute','chargingSchedule':[{'id':1,'chargingRateUnit':'W','chargingSchedulePeriod':[{'startPeriod':0,'limit':1000}]}]}}]))
        except Exception as e:
            from websockets.exceptions import ConnectionClosed
            if not isinstance(e,ConnectionClosed):errors.append(repr(e))
    server=serve(charger,'127.0.0.1',wsport,subprotocols=['ocpp1.6','ocpp2.0.1'],max_size=65536)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    env=os.environ.copy();env.update(SIM_ADMIN_TOKEN=token,SIM_PORT=str(port),SIM_API_HOST=host,SIM_PUBLIC_ORIGIN=origin,SIM_CSMS_URL=f'ws://127.0.0.1:{wsport}/ocpp/',SIM_STATION_SECRETS=json.dumps({'SIMREAL':station_secret}))
    env.pop('SIM_WEB_ROOT',None)
    process=subprocess.Popen([str(binary)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    count=0
    def request(path,body=None,expected=200,authorization=token,request_origin=origin,request_host=host,content='application/json'):
        nonlocal count
        headers={'Host':request_host}
        if authorization:headers['Authorization']='Bearer '+authorization
        if body is not None:headers.update({'Content-Type':content,'Origin':request_origin})
        data=body if isinstance(body,bytes) else json.dumps(body).encode() if body is not None else None
        req=urllib.request.Request(origin+path,data=data,headers=headers)
        try:
            with urllib.request.urlopen(req,timeout=15) as result:status=result.status;result_body=result.read()
        except urllib.error.HTTPError as e:status=e.code;result_body=e.read()
        assert status==expected,(path,status,expected,result_body[:100]);count+=1
        return json.loads(result_body) if result_body and status in (200,201,202) else None
    def state(id):return next(c for c in request('/api/v1/chargers')['chargers'] if c['id']==id)
    def action(id,name,**extras):return request('/api/v1/chargers/'+id+'/actions',{'action':name,**extras},202)
    def create(id,protocol,mode='mock'):request('/api/v1/chargers',{'id':id,'protocol':protocol,'mode':mode},201);wait(lambda:state(id)['registered'])
    try:
        def ready():
            try:return request('/health/ready')
            except (ConnectionError,urllib.error.URLError):return False
        wait(ready)
        request('/api/v1/chargers',authorization=None,expected=401)
        request('/api/v1/chargers',authorization='0'*64,expected=401)
        request('/api/v1/chargers',request_host='wrong.example',expected=400)
        request('/api/v1/chargers',{},request_origin='https://wrong.example',expected=403)
        request('/api/v1/chargers',{},content='text/plain',expected=415)
        request('/api/v1/chargers',b'{' ,expected=400)
        request('/api/v1/chargers',b'{"id":"SIM01","id":"SIM02"}',expected=400)
        request('/api/v1/chargers',b'['*30+b'0'+b']'*30,expected=400)
        request('/api/v1/chargers',b' '*9000,expected=413)
        request('/api/v1/chargers',{'id':'CP_REAL'},expected=400)
        request('/api/v1/chargers',{'id':'SIM01','capacityKwh':-1},expected=400)
        request('/api/v1/chargers',{'id':'SIMNOSECRET','mode':'real'},expected=400)
        for protocol,id in [('ocpp1.6','SIM16'),('ocpp2.0.1','SIM201')]:
            create(id,protocol);action(id,'plug');wait(lambda:state(id)['plugged'])
            action(id,'start',tag='DENY001');time.sleep(.4);assert state(id)['powerKw']==0
            action(id,'start',tag='TEST001');wait(lambda:state(id)['status']=='Charging')
            action(id,'configure',speed=60,gridKw=2);wait(lambda:0<state(id)['powerKw']<=2.001)
            wait(lambda:state(id)['energyKwh']>0)
            action(id,'pause');wait(lambda:state(id)['paused']);assert state(id)['powerKw']==0
            before=state(id)['energyKwh'];time.sleep(.4);assert state(id)['energyKwh']==before
            action(id,'resume');wait(lambda:state(id)['status']=='Charging')
            action(id,'network',enabled=False);wait(lambda:not state(id)['online']);time.sleep(5.3);assert state(id)['queued']>0
            action(id,'network',enabled=True);wait(lambda:state(id)['registered']);assert state(id)['queued']==0
            action(id,'fault',fault='EmergencyStop');wait(lambda:state(id)['status']=='Faulted');assert state(id)['powerKw']==0
            events=request('/api/v1/events')
            for event in events:
                if event['station']==id and event['type']=='out':validate(protocol,event['message'][2],event['message'][3])
            action(id,'fault',fault='NoError');action(id,'unplug');wait(lambda:state(id)['status']=='Available');action(id,'remove')
        for protocol in ('ocpp1.6','ocpp2.0.1'):
            ack_started.clear();create('SIMREAL',protocol,'real');action('SIMREAL','plug');wait(lambda:state('SIMREAL')['plugged']);action('SIMREAL','start')
            wait(lambda:state('SIMREAL')['status']=='Charging');assert ack_started.wait(2)
            wait(lambda:state('SIMREAL')['gridKw']==1)
            wait(lambda:any(m[0]==4 and m[1]=='unsupported' for m in commands))
            time.sleep(5.2);action('SIMREAL','stop');wait(lambda:state('SIMREAL')['status']=='Finishing')
            action('SIMREAL','unplug');wait(lambda:state('SIMREAL')['status']=='Available');action('SIMREAL','remove')
            # Stop while the start acknowledgement is still in flight: exactly one end, no ghost charging.
            before=sum(1 for p,n,b in frames if p==protocol and(n=='StopTransaction' or(n=='TransactionEvent' and b['eventType']=='Ended')))
            start_received.clear();delay_start.set();create('SIMREAL',protocol,'real')
            action('SIMREAL','plug');wait(lambda:state('SIMREAL')['plugged']);action('SIMREAL','start')
            assert start_received.wait(3);action('SIMREAL','stop')
            wait(lambda:state('SIMREAL')['status']=='Finishing');assert state('SIMREAL')['powerKw']==0
            wait(lambda:sum(1 for p,n,b in frames if p==protocol and(n=='StopTransaction' or(n=='TransactionEvent' and b['eventType']=='Ended')))==before+1)
            action('SIMREAL','unplug');wait(lambda:state('SIMREAL')['status']=='Available');action('SIMREAL','remove')
        assert not errors,errors
        assert {'ocpp1.6','ocpp2.0.1'}==set(connections)
        assert any(p=='ocpp1.6' and n=='StopTransaction' for p,n,b in frames)
        assert any(p=='ocpp2.0.1' and n=='TransactionEvent' and b['eventType']=='Ended' for p,n,b in frames)
        print(f'Workflow suite passed: {count} HTTP checks/polls, both mock/real protocols, schema validation, faults, offline recovery and command replies')
    finally:
        if sys.exc_info()[0]:
            try:print('Last simulator events:',request('/api/v1/events')[-5:])
            except Exception:pass
        process.terminate()
        try:process.wait(timeout=10)
        except subprocess.TimeoutExpired:process.kill();process.wait()
        server.shutdown();thread.join(timeout=5)
        if errors:print('Fixture errors:',errors,file=sys.stderr)
        stderr=process.stderr.read().decode(errors='replace')
        if stderr:print('Backend diagnostics:',stderr)

if __name__=='__main__':suite(Path(sys.argv[1]).resolve())
