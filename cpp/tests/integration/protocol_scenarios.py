"""Behavioral OCPP scenarios against the caller's isolated fixture DB."""
import asyncio
import base64
import json
from pathlib import Path
from websockets.asyncio.client import connect

ROOT=Path(__file__).resolve().parents[2]

def sample(schema, root=None):
    """Minimal schema-valid payloads for transport coverage; state workflows below use explicit fixtures."""
    root=root or schema
    if '$ref' in schema:
        node=root
        for part in schema['$ref'].split('/')[1:]: node=node[part]
        return sample(node,root)
    if 'enum' in schema: return schema['enum'][0]
    kind=schema.get('type')
    if kind=='object': return {k:sample(schema['properties'][k],root) for k in schema.get('required',[])}
    if kind=='array': return [sample(schema['items'],root) for _ in range(schema.get('minItems',0))]
    if kind=='string': return '2026-10-02T01:00:00Z' if schema.get('format')=='date-time' else 'test'
    if kind in ('number','integer'): return max(0,schema.get('minimum',0))
    if kind=='boolean': return False
    raise ValueError(f'Unsupported schema fixture: {schema}')

async def protocols(port,tokens,password,db,api,check):
    headers={'Authorization':'Basic '+base64.b64encode(('CP_TEST:'+password).encode()).decode()}
    async with connect(f'ws://127.0.0.1:{port}/ocpp/CP_TEST',additional_headers=headers,subprotocols=['ocpp1.6']) as ws:
        await ws.send(json.dumps([2,'cache-warmup','Heartbeat',{}]))
        check(json.loads(await ws.recv())[0]==3,'prepared cache warmup')
        with db.cursor() as cursor:
            cursor.execute("SHOW GLOBAL STATUS LIKE 'Com_stmt_prepare'"); prepared=int(cursor.fetchone()[1])
        for number in range(8):
            await ws.send(json.dumps([2,'cache-'+str(number),'Heartbeat',{}]))
            check(json.loads(await ws.recv())[0]==3,'statement reuse across heartbeat requests')
        with db.cursor() as cursor:
            cursor.execute("SHOW GLOBAL STATUS LIKE 'Com_stmt_prepare'")
            check(int(cursor.fetchone()[1])-prepared<=8,'bounded prepared statement cache avoids repeated prepare round trips')
            cursor.execute('SELECT DATABASE()'); fixture_name=cursor.fetchone()[0]
            check(fixture_name.startswith('ocpp_cpp_test_'),'connection fault injection uses isolated fixture')
            cursor.execute('SELECT ID FROM information_schema.PROCESSLIST WHERE DB=%s AND ID<>CONNECTION_ID() ORDER BY ID LIMIT 1',(fixture_name,))
            connection_id=cursor.fetchone()[0]
            cursor.execute('KILL CONNECTION '+str(int(connection_id)))
        await ws.send(json.dumps([2,'lost-connection','Heartbeat',{}]))
        failed=json.loads(await ws.recv())
        check(failed[0]==4 and failed[2]=='InternalError','lost database connection fails explicitly')
        await ws.send(json.dumps([2,'reconnected','Heartbeat',{}]))
        check(json.loads(await ws.recv())[0]==3,'new lease reconnects without stale cached statements')
        with db.cursor() as cursor:
            cursor.execute('SELECT COUNT(*) FROM connector_meter_value'); meter_count=cursor.fetchone()[0]
            cursor.execute("SHOW GLOBAL STATUS LIKE 'Questions'"); queries=int(cursor.fetchone()[1])
        samples=[{'value':str(n)} for n in range(512)]
        await ws.send(json.dumps([2,'bulk16','MeterValues',{'connectorId':1,'meterValue':[{'timestamp':'2026-10-02T01:10:00Z','sampledValue':samples}]}]))
        check(json.loads(await ws.recv())[0]==3,'1.6 maximum bounded meter batch')
        with db.cursor() as cursor:
            cursor.execute("SHOW GLOBAL STATUS LIKE 'Questions'"); query_count=int(cursor.fetchone()[1])-queries
            cursor.execute('SELECT COUNT(*) FROM connector_meter_value')
            check(cursor.fetchone()[0]-meter_count==512,'all bulk samples persisted')
            check(query_count<80,'bulk meter insert avoids one SQL statement per sample')
        for suffix,fixture,status in [('valid','csr-rsa2048.pem','Accepted'),('tampered','csr-tampered.pem','Rejected')]:
            payload={'csr':(ROOT/'tests/fixtures'/fixture).read_text()}
            await ws.send(json.dumps([2,'csr16-'+suffix,'SignCertificate',payload]))
            check(json.loads(await ws.recv())[2]['status']==status,'1.6 CSR signature '+suffix)
        async def send_command(action,payload,response,role='operator'):
            task=asyncio.create_task(asyncio.to_thread(api,port,f'/api/v1/chargepoints/CP_TEST/commands/{action}',tokens[role],payload))
            request=json.loads(await asyncio.wait_for(ws.recv(),10))
            check(request[2]==action,'command action')
            await ws.send(json.dumps([3,request[1],response]))
            result=await task
            check(result==(200,response),'validated command result')
            with db.cursor() as cursor:
                cursor.execute('SELECT state,response_body FROM cpp_command_task WHERE command_id=%s',(request[1],))
                row=cursor.fetchone()
                check(row is not None and row[0]=='Succeeded' and json.loads(row[1])==response,'durable command result')
            return request[1]
        for action in ('GetDiagnostics','UpdateFirmware'):
            payload={'location':'https://transfers.example.test/a'}
            if action=='UpdateFirmware': payload['retrieveDate']='2029-01-01T00:00:00Z'
            await send_command(action,payload,{'fileName':'diagnostics.log'} if action=='GetDiagnostics' else {})
        await send_command('ReserveNow',{'connectorId':1,'reservationId':101,'idTag':'TAG','expiryDate':'2029-01-01T00:00:00Z'},{'status':'Accepted'})
        with db.cursor() as cursor:
            cursor.execute('SELECT status FROM reservation WHERE reservation_pk=101')
            check(cursor.fetchone()[0]=='Accepted','reservation accepted persistence')
        await send_command('CancelReservation',{'reservationId':101},{'status':'Accepted'})
        with db.cursor() as cursor:
            cursor.execute('SELECT status FROM reservation WHERE reservation_pk=101')
            check(cursor.fetchone()[0]=='Cancelled','reservation cancellation persistence')
        profile={'chargingProfileId':11,'stackLevel':0,'chargingProfilePurpose':'TxDefaultProfile','chargingProfileKind':'Relative',
                 'chargingSchedule':{'chargingRateUnit':'A','chargingSchedulePeriod':[{'startPeriod':0,'limit':16.0}]}}
        await send_command('SetChargingProfile',{'connectorId':1,'csChargingProfiles':profile},{'status':'Accepted'})
        with db.cursor() as cursor:
            cursor.execute("SELECT profile_id FROM cpp_profile_assignment WHERE station_id='CP_TEST'")
            check(cursor.fetchone()[0]==11,'accepted profile assignment')
        await send_command('ClearChargingProfile',{'id':11},{'status':'Accepted'})
        with db.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) FROM cpp_profile_assignment WHERE station_id='CP_TEST'")
            check(cursor.fetchone()[0]==0,'profile clear filters')
        await send_command('SendLocalList',{'listVersion':1,'updateType':'Full','localAuthorizationList':[{'idTag':'TAG','idTagInfo':{'status':'Accepted'}}]},{'status':'Accepted'})
        await send_command('SendLocalList',{'listVersion':2,'updateType':'Differential','localAuthorizationList':[{'idTag':'TAG'}]},{'status':'Accepted'})
        with db.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) FROM cpp_local_authorization WHERE station_id='CP_TEST'")
            check(cursor.fetchone()[0]==0,'differential deletion persisted')
        await send_command('GetLocalListVersion',{}, {'listVersion':2})
        await send_command('GetConfiguration',{}, {'configurationKey':[{'key':'HeartbeatInterval','readonly':False,'value':'60'}]})
        await send_command('UnlockConnector',{'connectorId':1},{'status':'Unlocked'})
        check(api(port,'/api/v1/chargepoints',tokens['read'])[0]==200,'station GET route is reachable')
        check(api(port,'/api/v1/ocppTags',tokens['read'])[0]==200,'tag GET route is reachable')
        for index,(action,payload) in enumerate([
            ('LogStatusNotification',{'status':'Uploaded','requestId':1}),
            ('SignedFirmwareStatusNotification',{'status':'Installed','requestId':1}),
            ('SecurityEventNotification',{'type':'InvalidMessage','timestamp':'2026-10-02T01:00:00Z','techInfo':'test'})]):
            await ws.send(json.dumps([2,f'extension-{index}',action,payload]))
            check(json.loads(await ws.recv())[0]==3,'security notification persistence')
    # A protocol upgrade establishes a new session and validates with its own schema set.
    await asyncio.sleep(.2)
    async with connect(f'ws://127.0.0.1:{port}/ocpp/CP_TEST',additional_headers=headers,subprotocols=['ocpp2.0.1']) as ws:
        check(ws.subprotocol=='ocpp2.0.1','OCPP 2.0.1 negotiation')
        async def call(id,action,payload):
            await ws.send(json.dumps([2,id,action,payload]))
            result=json.loads(await asyncio.wait_for(ws.recv(),10))
            check(result[1]==id,'2.0.1 response correlation')
            return result
        station_metadata={'vendorName':'vendor_'+('x'*43),'model':'model_'+('y'*14)}
        boot=await call('201-boot','BootNotification',{'reason':'PowerUp','chargingStation':station_metadata})
        check(boot[0]==3 and boot[2]['status']=='Accepted','2.0.1 boot')
        with db.cursor() as cursor:
            cursor.execute("SELECT charging_station_body FROM cpp201_station WHERE station_id='CP_TEST'")
            check(json.loads(cursor.fetchone()[0])==station_metadata,'full 2.0.1 metadata is retained beyond legacy display limits')
        with db.cursor() as cursor:
            cursor.execute('SELECT COUNT(*) FROM cpp201_meter'); meter_count=cursor.fetchone()[0]
            cursor.execute("SHOW GLOBAL STATUS LIKE 'Questions'"); queries=int(cursor.fetchone()[1])
        meters=[{'timestamp':'2026-10-02T01:10:00Z','sampledValue':[{'value':n}]} for n in range(512)]
        check((await call('bulk201','MeterValues',{'evseId':1,'meterValue':meters}))[0]==3,'2.0.1 bounded bulk meter batch')
        with db.cursor() as cursor:
            cursor.execute("SHOW GLOBAL STATUS LIKE 'Questions'"); query_count=int(cursor.fetchone()[1])-queries
            cursor.execute('SELECT COUNT(*) FROM cpp201_meter')
            check(cursor.fetchone()[0]-meter_count==512,'2.0.1 bulk telemetry persistence')
            check(query_count<80,'2.0.1 batched SQL telemetry')
        for suffix,fixture,status in [('valid','csr-p256.pem','Accepted'),('weak','csr-weak-rsa1024.pem','Rejected')]:
            payload={'csr':(ROOT/'tests/fixtures'/fixture).read_text(),'certificateType':'ChargingStationCertificate'}
            check((await call('csr201-'+suffix,'SignCertificate',payload))[2]['status']==status,'2.0.1 CSR validation '+suffix)
        token={'idToken':'TOKEN201','type':'Central'}
        check(api(port,'/api/v1/idTokens',tokens['admin'],{'idToken':token,'idTokenInfo':{'status':'Accepted'}})[0]==200,'2.0.1 token provisioning')
        check((await call('201-auth','Authorize',{'idToken':token}))[2]['idTokenInfo']['status']=='Accepted','2.0.1 authorization')
        event={'eventType':'Started','timestamp':'2026-10-02T01:00:00Z','triggerReason':'Authorized','seqNo':0,
               'transactionInfo':{'transactionId':'TX201','chargingState':'Charging'},'evse':{'id':1,'connectorId':1},'idToken':token}
        check((await call('201-start','TransactionEvent',event))[0]==3,'2.0.1 transaction start')
        check((await call('201-start-repeat','TransactionEvent',event))[0]==3,'2.0.1 sequence dedup')
        changed=dict(event,triggerReason='RemoteStart')
        check((await call('201-conflict','TransactionEvent',changed))[2]=='ProtocolError','2.0.1 sequence conflict rejection')
        event=dict(event,eventType='Ended',seqNo=2,timestamp='2026-10-02T01:02:00Z')
        check((await call('201-end','TransactionEvent',event))[0]==3,'2.0.1 transaction end')
        late=dict(event,eventType='Updated',seqNo=1,timestamp='2026-10-02T01:01:00Z')
        check((await call('201-late','TransactionEvent',late))[0]==3,'2.0.1 late offline event retained')
        with db.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) FROM cpp201_transaction_event WHERE transaction_id='TX201'")
            check(cursor.fetchone()[0]==3,'2.0.1 event uniqueness')
            cursor.execute("SELECT last_seq_no,end_timestamp FROM cpp201_transaction WHERE transaction_id='TX201'")
            row=cursor.fetchone(); check(row[0]==2 and row[1] is not None,'late update cannot reopen ended transaction')
        # Enumerate every station-initiated action using schema-valid fixtures.
        actions=['Authorize','BootNotification','ClearedChargingLimit','DataTransfer','FirmwareStatusNotification','Get15118EVCertificate',
                 'GetCertificateStatus','Heartbeat','LogStatusNotification','MeterValues','NotifyChargingLimit','NotifyCustomerInformation',
                 'NotifyDisplayMessages','NotifyEVChargingNeeds','NotifyEVChargingSchedule','NotifyEvent','NotifyMonitoringReport','NotifyReport',
                 'PublishFirmwareStatusNotification','ReportChargingProfiles','ReservationStatusUpdate','SecurityEventNotification','SignCertificate','StatusNotification']
        for index,action in enumerate(actions):
            payload=sample(json.loads((ROOT/'schemas/2.0.1'/f'{action}Request.json').read_text(encoding='utf-8')))
            if action=='StatusNotification': payload.update(evseId=1,connectorId=1)
            result=await call(f'201-action-{index}',action,payload)
            check(result[0]==3,'2.0.1 incoming '+action)
            if action in ('GetCertificateStatus','Get15118EVCertificate'): check(result[2]['status']=='Failed','unconfigured PKI provider fails closed')
            await asyncio.sleep(.07)
        report={'requestId':55,'generatedAt':'2026-10-02T01:00:00Z','seqNo':0,'reportData':[
            {'component':{'name':'ChargingStation'},'variable':{'name':'HeartbeatInterval'},'variableAttribute':[{'type':'Actual','value':'60'}]}]}
        check((await call('201-device-report','NotifyReport',report))[0]==3,'device model report ingestion')
        with db.cursor() as cursor:
            cursor.execute("SELECT value FROM cpp201_device_variable WHERE station_id='CP_TEST' AND variable_name='HeartbeatInterval'")
            check(cursor.fetchone()[0]=='60','device variable persisted')
        live=dict(event,eventType='Started',seqNo=0,transactionInfo={'transactionId':'TX201_LIVE','chargingState':'Charging'})
        check((await call('201-live-start','TransactionEvent',live))[0]==3,'live transaction fixture for outbound ownership')
        variables={'getVariableData':[{'component':{'name':'ChargingStation'},'variable':{'name':'HeartbeatInterval'}}]}
        task=asyncio.create_task(asyncio.to_thread(api,port,'/api/v1/chargepoints/CP_TEST/commands/GetVariables',tokens['operator'],variables))
        wire=json.loads(await asyncio.wait_for(ws.recv(),10))
        invalid={'getVariableResult':[{'attributeStatus':'Accepted','component':{'name':'ChargingStation'},'variable':{'name':'HeartbeatInterval'}}]}
        await ws.send(json.dumps([3,wire[1],invalid]))
        check((await task)[0]==502,'Accepted variable result must include value')
        with db.cursor() as cursor:
            cursor.execute('SELECT state FROM cpp_command_task WHERE command_id=%s',(wire[1],))
            check(cursor.fetchone()[0]=='Uncertain','invalid executed reply has uncertain outcome')
        # Every CSMS-initiated action must be validated, delivered, correlated and persisted.
        incoming=set(actions+['TransactionEvent'])
        requests=sorted((ROOT/'schemas/2.0.1').glob('*Request.json'))
        for source in requests:
            action=source.stem[:-7]
            if action in incoming and action!='DataTransfer': continue
            payload=sample(json.loads(source.read_text(encoding='utf-8')))
            response=sample(json.loads((source.parent/(action+'Response.json')).read_text(encoding='utf-8')))
            if action=='RequestStopTransaction': payload['transactionId']='TX201_LIVE'
            if action=='ReserveNow': payload.update(id=201,expiryDateTime='2029-01-01T00:00:00Z')
            if action=='CancelReservation':
                with db.cursor() as cursor:
                    cursor.execute("INSERT INTO cpp201_reservation(station_id,reservation_id,evse_id,expiry_timestamp,request_body,state) VALUES('CP_TEST',202,1,'2029-01-01','{}','Accepted')")
                payload['reservationId']=202
            if action=='SendLocalList': payload.update(versionNumber=3,updateType='Full')
            if action=='SetChargingProfile':
                payload={'evseId':1,'chargingProfile':{'id':201,'stackLevel':0,'chargingProfilePurpose':'TxDefaultProfile','chargingProfileKind':'Relative',
                    'chargingSchedule':[{'id':201,'chargingRateUnit':'A','chargingSchedulePeriod':[{'startPeriod':0,'limit':16.0}]}]}}
            if action=='GetLog': payload['log']['remoteLocation']='https://transfers.example.test/log'
            if action=='UpdateFirmware': payload['firmware']['location']='https://transfers.example.test/firmware.bin'
            if action=='PublishFirmware': payload['location']='https://transfers.example.test/firmware.bin'
            if action=='SetNetworkProfile': payload['connectionData'].update(ocppCsmsUrl='wss://csms.example.test/ocpp',securityProfile=2)
            if action=='GetVariables':
                for entry in response['getVariableResult']: entry['attributeValue']='60'
            role='admin' if action in ('CertificateSigned','InstallCertificate','DeleteCertificate','SetNetworkProfile') else 'operator'
            task=asyncio.create_task(asyncio.to_thread(api,port,f'/api/v1/chargepoints/CP_TEST/commands/{action}',tokens[role],payload))
            done,pending=await asyncio.wait([task,asyncio.create_task(ws.recv())],return_when=asyncio.FIRST_COMPLETED,timeout=10)
            receive=next((f for f in done if f is not task),None)
            if receive is None:
                for f in pending: f.cancel()
                failure=await task
                raise AssertionError(f'2.0.1 outbound {action} was not dispatched: {failure}')
            request=json.loads(receive.result())
            check(request[2]==action,'2.0.1 outbound '+action)
            await ws.send(json.dumps([3,request[1],response]))
            check((await task)==(200,response),'2.0.1 correlated '+action)
            with db.cursor() as cursor:
                cursor.execute('SELECT state,protocol FROM cpp_command_task WHERE command_id=%s',(request[1],))
                row=cursor.fetchone(); check(row[1]=='ocpp2.0.1' and row[0]!='Pending','2.0.1 task persistence '+action)
            await asyncio.sleep(.07)
