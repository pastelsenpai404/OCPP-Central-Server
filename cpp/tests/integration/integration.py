"""Destructive only inside a NEW random test database on a caller-supplied local test DB.
Never accepts a remote DB host. No production defaults or credentials.
pip install websockets==15.0.1 PyMySQL==1.1.2
"""
import argparse
import asyncio
import base64
import json
import os
from pathlib import Path
import secrets
import subprocess
import time
import statistics
import urllib.error
import urllib.request

import pymysql
from websockets.asyncio.client import connect
from websockets.exceptions import InvalidStatus, ConnectionClosed

ROOT = Path(__file__).resolve().parents[2]
CHECKS = 0

def check(value, label):
    global CHECKS
    CHECKS += 1
    if not value:
        raise AssertionError(label)

def api(port, path, token=None, body=None, origin=None):
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    if origin is not None:
        headers["Origin"] = origin
    request = urllib.request.Request(f"http://127.0.0.1:{port}{path}", headers=headers,
        data=None if body is None else json.dumps(body).encode())
    try:
        response = urllib.request.build_opener(urllib.request.ProxyHandler({})).open(request, timeout=35)
    except urllib.error.HTTPError as error:
        response = error
    return response.status, json.loads(response.read())

def fixture(cursor):
    # Checked-in definitions extracted from the legacy reference, without historical data.
    text=(ROOT/'tests/fixtures/legacy-schema.sql').read_text(encoding='utf-8')
    text='\n'.join(line for line in text.splitlines() if not line.lstrip().startswith('--'))
    for sql in text.split(';'):
        if sql.strip(): cursor.execute(sql)
    for migration in sorted((ROOT / "migrations").glob("*.sql")):
        text='\n'.join(line for line in migration.read_text().splitlines() if not line.lstrip().startswith('--'))
        for statement in text.split(";"):
            lines = [line for line in statement.splitlines() if not line.startswith("--")]
            sql = "\n".join(lines).strip()
            if sql: cursor.execute(sql)
    cursor.execute("INSERT INTO settings(app_id,heartbeat_interval_in_seconds) VALUES('test',60)")
    cursor.execute("INSERT INTO charge_box(charge_box_id) VALUES('CP_TEST'),('OTHER')")
    cursor.execute("INSERT INTO ocpp_tag(id_tag,max_active_transaction_count) VALUES('TAG',1)")

async def scenarios(port, tokens, password, db):
    probe=ROOT/('static-probe-'+secrets.token_hex(8)+'.txt')
    try:
        probe.write_text('private-test-sentinel',encoding='utf-8')
        request=urllib.request.Request(f'http://127.0.0.1:{port}/{probe.name}')
        try:
            response=urllib.request.build_opener(urllib.request.ProxyHandler({})).open(request,timeout=5)
        except urllib.error.HTTPError as error:
            response=error
        with response:
            check(response.status==404,'working-directory files are not public static assets')
    finally:
        probe.unlink(missing_ok=True)
    uri = f"ws://127.0.0.1:{port}/ocpp/CP_TEST"
    header = {"Authorization": "Basic " + base64.b64encode(("CP_TEST:"+password).encode()).decode()}
    for auth, protocols in (({}, ["ocpp1.6"]), (header, ["ocpp1.7"])):
        try:
            async with connect(uri, additional_headers=auth, subprotocols=protocols):
                check(False, "unauthorized handshake allowed")
        except InvalidStatus as error:
            check(error.response.status_code in (400, 401), "handshake rejected")
    check(api(port,"/api/v1/transactions")[0] == 401, "REST authentication")
    check(api(port,"/api/v1/admin/session")[0]==401,"UI session metadata requires authentication")
    reader_session=api(port,"/api/v1/admin/session",tokens['read'])
    check(reader_session[0]==200 and reader_session[1]['role']==1 and not reader_session[1]['configuredStations'],"reader session metadata never returns configured credentials")
    catalog=api(port,"/api/v1/admin/commands",tokens['read'])
    check(catalog[0]==200 and len(catalog[1]['1.6'])==26 and len(catalog[1]['2.0.1'])==40,"UI command catalog covers all CSMS directions")
    check(api(port,"/api/v1/idTokens",tokens['read'])[0]==401,"id-token read requires admin")
    check(api(port,"/api/v1/idTokens",tokens['admin'])[0]==200,"admin id-token GET is reachable alongside POST")
    check(api(port,"/api/v1/ocppTags",tokens['admin'],{'idTag':'UI_ORIGIN','maxActiveTransactions':0},f'http://127.0.0.1:{port}')[0]==200,"same-origin UI POST with bearer authentication")
    check(api(port,"/api/v1/ocppTags",tokens['admin'],{'idTag':'EXTERNAL_ORIGIN','maxActiveTransactions':0},'https://external.example')[0]==403,"external browser origin denied despite valid token")
    with db.cursor() as cursor:
        cursor.execute("SELECT COUNT(*) FROM ocpp_tag WHERE id_tag='EXTERNAL_ORIGIN'")
        check(cursor.fetchone()[0]==0,"denied browser POST has no database side effects")
    check(api(port,"/api/v1/transactions",tokens["read"])[0] == 200, "reader transaction list")
    check(api(port,"/api/v1/transactions?offset=invalid",tokens["read"])[0] == 400, "pagination validation")
    check(api(port,"/api/v1/chargepoints",tokens["read"],{"chargeBoxId":"OTHER"})[0] == 401, "reader cannot provision")
    check(api(port,"/api/v1/ocppTags",tokens["admin"],{"idTag":"BLOCKED","maxActiveTransactions":0})[0] == 200, "admin tag write")
    check(api(port,"/api/v1/chargepoints/CP_TEST/commands/GetDiagnostics",tokens["operator"],{"location":"http://127.0.0.1/"})[0] == 400, "URL command disabled")
    async with connect(uri,additional_headers=header,subprotocols=["ocpp1.6"]) as ws:
        check(ws.subprotocol == "ocpp1.6", "subprotocol negotiated")
        async def fragmented_with_ping():
            yield '[2,"fragmented","Heartbeat",'
            waiter = await ws.ping(b"interleaved")
            await asyncio.wait_for(waiter, 5)
            yield '{}]'
        await ws.send(fragmented_with_ping())
        fragmented=json.loads(await asyncio.wait_for(ws.recv(),10))
        check(fragmented[0]==3 and fragmented[1]=="fragmented","valid fragments with interleaved ping")
        async def call(id, action, payload):
            await ws.send(json.dumps([2,id,action,payload]))
            result=json.loads(await asyncio.wait_for(ws.recv(),10))
            check(result[1]==id,"response correlation")
            return result
        boot=await call("boot","BootNotification",{"chargePointVendor":"test","chargePointModel":"model"})
        check(boot[0]==3 and boot[2]["status"]=="Accepted","boot accepted")
        check((await call("heartbeat","Heartbeat",{}))[0]==3,"heartbeat")
        check((await call("auth","Authorize",{"idTag":"TAG"}))[2]["idTagInfo"]["status"]=="Accepted","authorization")
        check(api(port,"/api/v1/ocppTags",tokens["admin"],{"idTag":"TAG","maxActiveTransactions":0})[0]==200,"block active tag")
        check((await call("auth","Authorize",{"idTag":"TAG"}))[2]["idTagInfo"]["status"]=="Blocked","authorization replay must reflect revocation")
        check(api(port,"/api/v1/ocppTags",tokens["admin"],{"idTag":"TAG","maxActiveTransactions":1})[0]==200,"restore active tag")
        check((await call("blocked","Authorize",{"idTag":"BLOCKED"}))[2]["idTagInfo"]["status"]=="Blocked","blocked tag")
        check((await call("bad","StatusNotification",{"connectorId":-1,"status":"Available","errorCode":"NoError"}))[0]==4,"bad payload")
        start={"connectorId":1,"idTag":"TAG","timestamp":"2026-10-02T01:00:00Z","meterStart":100}
        first=await call("start","StartTransaction",start)
        check(first[0]==3,"transaction started")
        tx=first[2]["transactionId"]
        check(await call("start","StartTransaction",start)==first,"retry stable response")
        check((await call("start-again","StartTransaction",start))[2]["transactionId"]==tx,"semantic transaction deduplication")
        changed=dict(start,meterStart=101)
        check((await call("start","StartTransaction",changed))[2]=="ProtocolError","conflicting message ID")
        meters={"connectorId":1,"transactionId":tx,"meterValue":[{"timestamp":"2026-10-02T01:01:00Z","sampledValue":[{"value":"120"}]}]}
        check((await call("meter","MeterValues",meters))[0]==3,"meter persistence")
        check((await call("status","StatusNotification",{"connectorId":1,"status":"Available","errorCode":"NoError"}))[0]==3,"connector status")
        check((await call("firmware","FirmwareStatusNotification",{"status":"Installed"}))[0]==3,"firmware status")
        check((await call("diagnostic","DiagnosticsStatusNotification",{"status":"Uploaded"}))[0]==3,"diagnostic status")
        check((await call("data","DataTransfer",{"vendorId":"test","data":"payload"}))[0]==3,"data transfer")
        task=asyncio.create_task(asyncio.to_thread(api,port,"/api/v1/chargepoints/CP_TEST/commands/RemoteStopTransaction",tokens["operator"],{"transactionId":tx}))
        request=json.loads(await asyncio.wait_for(ws.recv(),10))
        check(request[0]==2 and request[2]=="RemoteStopTransaction","outbound command")
        await ws.send(json.dumps([3,request[1],{"status":"Accepted"}]))
        check((await task)==(200,{"status":"Accepted"}),"command result")
        invalid_task=asyncio.create_task(asyncio.to_thread(api,port,"/api/v1/chargepoints/CP_TEST/commands/Reset",tokens["operator"],{"type":"Soft"}))
        invalid_request=json.loads(await asyncio.wait_for(ws.recv(),10))
        await ws.send(json.dumps([3,invalid_request[1],{"status":"Unexpected"}]))
        check((await invalid_task)[0]==502,"invalid command response rejected")
        error_task=asyncio.create_task(asyncio.to_thread(api,port,"/api/v1/chargepoints/CP_TEST/commands/Reset",tokens["operator"],{"type":"Soft"}))
        error_request=json.loads(await asyncio.wait_for(ws.recv(),10))
        await ws.send(json.dumps([4,error_request[1],"NotSupported","not supported",{}]))
        check((await error_task)[0]==502,"CALLERROR correlation")
        other_header={"Authorization":"Basic "+base64.b64encode(("OTHER:"+password[::-1]).encode()).decode()}
        async with connect(f"ws://127.0.0.1:{port}/ocpp/OTHER",additional_headers=other_header,subprotocols=["ocpp1.6"]) as other:
            await other.send(json.dumps([2,"foreign","StopTransaction",{"transactionId":tx,"meterStop":120,"timestamp":"2026-10-02T01:02:00Z"}]))
            check(json.loads(await other.recv())[2]=="SecurityError","cross-station transaction denied")
        stop={"transactionId":tx,"meterStop":120,"timestamp":"2026-10-02T01:02:00Z","idTag":"TAG"}
        check((await call("stop","StopTransaction",stop))[0]==3,"transaction stopped")
        check((await call("stop-again","StopTransaction",stop))[0]==3,"duplicate stop accepted")
        with db.cursor() as cur:
            cur.execute("SELECT COUNT(*) FROM transaction_start")
            check(cur.fetchone()[0]==1,"single start row")
            cur.execute("SELECT COUNT(*) FROM transaction_stop")
            check(cur.fetchone()[0]==1,"single stop row")
            cur.execute("SELECT COUNT(*) FROM connector_meter_value")
            check(cur.fetchone()[0]==1,"single meter row")
            cur.execute("SELECT COUNT(*) FROM cpp_event_outbox WHERE action='StartTransaction'")
            check(cur.fetchone()[0]==1,"atomic deduplicated outbox")
        await ws.send('[2,"duplicate","Heartbeat",{"x":1,"x":2}]')
        try:
            await ws.recv()
            check(False,"duplicate key accepted")
        except ConnectionClosed:
            check(True,"malformed JSON closes socket")
    # New connection must still replay the committed response, with no in-memory cache.
    async with connect(uri,additional_headers=header,subprotocols=["ocpp1.6"]) as ws:
        await ws.send(json.dumps([2,"start","StartTransaction",start]))
        check(json.loads(await ws.recv())==first,"durable replay after reconnect")
        # Two individually legal frames exceed the aggregate 64KiB limit.
        try:
            await ws.send(["x"*40000,"x"*40000])
            await ws.recv()
            check(False,"fragment limit not enforced")
        except ConnectionClosed:
            check(True,"fragment aggregate limit")
    async with connect(uri,additional_headers=header,subprotocols=["ocpp1.6"]) as ws:
        ws.transport.write(b'\x81\x02{}')
        try:
            await ws.recv()
            check(False,"unmasked client frame accepted")
        except ConnectionClosed:
            check(True,"unmasked frame rejected")
    async with connect(uri,additional_headers=header,subprotocols=["ocpp1.6"]) as ws:
        timeout_task=asyncio.create_task(asyncio.to_thread(api,port,"/api/v1/chargepoints/CP_TEST/commands/Reset",tokens["operator"],{"type":"Soft"}))
        await asyncio.wait_for(ws.recv(),10)
        check((await timeout_task)[0]==504,"bounded command timeout")

async def load_scenario(port, station_passwords, rounds, rate):
    latencies=[]
    async def station_run(station,password):
        header={"Authorization":"Basic "+base64.b64encode((station+":"+password).encode()).decode()}
        async with connect(f"ws://127.0.0.1:{port}/ocpp/{station}",additional_headers=header,subprotocols=["ocpp1.6"]) as ws:
            next_send=time.perf_counter()
            for n in range(rounds):
                await asyncio.sleep(max(0,next_send-time.perf_counter()))
                start=time.perf_counter()
                await ws.send(json.dumps([2,str(n),"Heartbeat",{}]))
                response=json.loads(await asyncio.wait_for(ws.recv(),15))
                if response[0]!=3 or response[1]!=str(n):
                    raise RuntimeError("Load response mismatch")
                latencies.append((time.perf_counter()-start)*1000)
                next_send+=1/rate
    start=time.perf_counter()
    await asyncio.gather(*(station_run(station,password) for station,password in station_passwords.items()))
    duration=time.perf_counter()-start
    ordered=sorted(latencies)
    percentile=lambda p:ordered[min(len(ordered)-1,int((len(ordered)-1)*p))]
    result={"stations":len(station_passwords),"messages":len(latencies),"requested_rate_per_station":rate,
        "duration_seconds":round(duration,3),"completed_messages_per_second":round(len(latencies)/duration,2),
        "p50_ms":round(statistics.median(ordered),3),"p95_ms":round(percentile(.95),3),"p99_ms":round(percentile(.99),3),
        "errors":0,"workload":"Heartbeat only; loopback plaintext WebSocket + MySQL station lock/update/commit; no TLS or billing"}
    print(json.dumps(result,indent=2))
    (ROOT/"load-results.json").write_text(json.dumps(result,indent=2))

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--server",type=Path,required=True)
    parser.add_argument("--db-port",type=int,default=33077)
    parser.add_argument("--port",type=int,default=35003)
    parser.add_argument("--load-stations",type=int,default=0)
    parser.add_argument("--load-rounds",type=int,default=50)
    parser.add_argument("--load-rate",type=float,default=10)
    parser.add_argument("--ui",action="store_true",help="Run Playwright UI checks against this isolated fixture")
    args=parser.parse_args()
    root_password=os.environ["OCPP_TEST_DB_PASSWORD"]
    name="ocpp_cpp_test_"+secrets.token_hex(6)
    # Explicit caller credentials and loopback only. Create and drop this random DB.
    db=pymysql.connect(host="127.0.0.1",port=args.db_port,user="root",password=root_password,autocommit=True)
    tokens={role:secrets.token_hex(32) for role in ("read","operator","admin")}
    password=secrets.token_hex(32)
    if not 0<=args.load_stations<=200 or not 1<=args.load_rounds<=1000 or not 0<args.load_rate<=15:
        raise ValueError("Invalid load test limits")
    station_passwords={f"LOAD_{n}":secrets.token_hex(32) for n in range(args.load_stations)}
    process=None
    from soap_device import SoapDevice,outgoing_soap
    soap_passwords={station:secrets.token_hex(32) for station in ('SOAP_12','SOAP_15','SOAP_16')}
    device=SoapDevice(soap_passwords)
    device.start()
    try:
        with db.cursor() as cur:
            cur.execute("CREATE DATABASE "+name+" CHARACTER SET utf8mb4")
            cur.execute("USE "+name)
            fixture(cur)
            cur.executemany("INSERT INTO charge_box(charge_box_id) VALUES(%s)",[(station,) for station in soap_passwords])
            if station_passwords:
                cur.executemany("INSERT INTO charge_box(charge_box_id) VALUES(%s)",[(station,) for station in station_passwords])
        env=dict(os.environ,OCPP_DB_NAME=name,OCPP_DB_USER="root",OCPP_DB_PASSWORD=root_password,
            OCPP_DB_HOST="127.0.0.1",OCPP_DB_PORT=str(args.db_port),OCPP_PORT=str(args.port),
            OCPP_READ_TOKEN=tokens["read"],OCPP_OPERATOR_TOKEN=tokens["operator"],OCPP_ADMIN_TOKEN=tokens["admin"],
            OCPP_TRANSFER_ORIGINS='["https://transfers.example.test"]',OCPP_DB_CA='',
            OCPP_SOAP_ORIGINS=json.dumps([device.origin]),
            OCPP_SOAP_ENDPOINTS=json.dumps({station:{'version':version,'url':device.origin+'/'+version} for station,version in [('SOAP_12','1.2'),('SOAP_15','1.5'),('SOAP_16','1.6')]}),
            OCPP_STATION_SECRETS=json.dumps((station_passwords or {"CP_TEST":password,"OTHER":password[::-1]})|soap_passwords))
        flags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0
        with (ROOT/"integration-server.log").open("w") as log:
            process=subprocess.Popen([str(args.server.resolve()),str(ROOT/"schemas")],env=env,stdout=log,stderr=log,creationflags=flags,cwd=ROOT)
            readiness = None
            for _ in range(100):
                if process.poll() is not None:
                    raise RuntimeError("Server exited; see integration-server.log")
                try:
                    readiness = api(args.port,"/health/ready")
                    if readiness[0]==200:
                        break
                except OSError as error:
                    readiness = str(error)
                time.sleep(.1)
            else:
                raise RuntimeError(f"Server not ready: {readiness}")
            if station_passwords:
                asyncio.run(load_scenario(args.port,station_passwords,args.load_rounds,args.load_rate))
            else:
                asyncio.run(scenarios(args.port,tokens,password,db))
                from protocol_scenarios import protocols
                asyncio.run(protocols(args.port,tokens,password,db,api,check))
                from legacy_scenarios import legacy
                asyncio.run(legacy(args.port,tokens,password,db,api,check))
                asyncio.run(outgoing_soap(args.port,tokens,device,api,check))
                from billing_scenarios import sandbox
                sandbox(args.port,tokens,db,api,check)
                if args.ui:
                    from ui_scenarios import admin_ui
                    admin_ui(args.port,tokens,check)
                # Crash after confirmed dispatch and before reply. Startup must retain
                # an uncertain outcome, without sending the command a second time.
                from concurrent.futures import ThreadPoolExecutor
                received=len(device.requests)
                device.mode='hold'
                with ThreadPoolExecutor(max_workers=1) as client:
                    pending=client.submit(api,args.port,'/api/v1/chargepoints/SOAP_16/commands/Reset',tokens['operator'],{'type':'Soft'})
                    for _ in range(100):
                        if len(device.requests)>received: break
                        time.sleep(.05)
                    else: raise AssertionError('crash fixture command did not reach device')
                    with db.cursor() as cursor:
                        cursor.execute("SELECT command_id FROM cpp_command_task WHERE station_id='SOAP_16' AND state='Pending' ORDER BY created_at DESC LIMIT 1")
                        crash_id=cursor.fetchone()[0]
                    process.kill();process.wait(timeout=10)
                    device.mode='normal';device.release.set()
                    try: pending.result(timeout=5)
                    except Exception: check(True,'client observes interrupted command during crash')
                    else: check(False,'crash unexpectedly returned a known command result')
                process=subprocess.Popen([str(args.server.resolve()),str(ROOT/'schemas')],env=env,stdout=log,stderr=log,creationflags=flags,cwd=ROOT)
                for _ in range(100):
                    if process.poll() is not None: raise RuntimeError('Restarted server exited')
                    try:
                        if api(args.port,'/health/ready')[0]==200: break
                    except OSError: pass
                    time.sleep(.1)
                else: raise AssertionError('restarted server not ready')
                task=api(args.port,'/api/v1/tasks/'+crash_id,tokens['operator'])
                check(task[0]==200 and task[1]['state']=='Uncertain','durable uncertain command recovery after actual process crash')
                check(len(device.requests)==received+1,'recovery never resends an ambiguous command')
                print(f"PASS: {CHECKS} integration assertions")
    finally:
        device.close()
        if process:
            process.terminate()
            try:process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill();process.wait()
        with db.cursor() as cur:
            cur.execute("DROP DATABASE "+name)
        db.close()

if __name__=="__main__":
    main()
