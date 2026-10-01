"""Legacy wire interoperability and hostile XML against an isolated database."""
import asyncio
import base64
import json
import urllib.request
import urllib.error
import xml.etree.ElementTree as ET
from websockets.asyncio.client import connect

async def legacy(port,tokens,password,db,api,check):
    authorization='Basic '+base64.b64encode(('CP_TEST:'+password).encode()).decode()
    for version in ('1.2','1.5'):
        async with connect(f'ws://127.0.0.1:{port}/ocpp/CP_TEST',additional_headers={'Authorization':authorization},subprotocols=['ocpp'+version]) as ws:
            check(ws.subprotocol=='ocpp'+version,'legacy negotiation '+version)
            async def call(action,payload,identifier):
                await ws.send(json.dumps([2,identifier,action,payload]))
                result=json.loads(await asyncio.wait_for(ws.recv(),5))
                check(result[0]==3,'legacy incoming '+version+' '+action)
                return result[2]
            boot=await call('BootNotification',{'chargePointVendor':'test','chargePointModel':'test'},'legacy-boot')
            check(boot['heartbeatInterval']==60 and 'interval' not in boot,'legacy heartbeat interval')
            await call('Authorize',{'idTag':'TAG'},'legacy-authorize')
            tx=(await call('StartTransaction',{'connectorId':1,'idTag':'TAG','timestamp':'2026-10-02T04:00:00Z' if version=='1.2' else '2026-10-02T05:00:00Z','meterStart':200},'same-start'))['transactionId']
            meter={'timestamp':'2026-10-02T04:01:00Z','value':210 if version=='1.2' else [{'value':'210','unit':'Wh'}]}
            await call('MeterValues',{'connectorId':1,'values':[meter]},'same-meter')
            await call('MeterValues',{'connectorId':1},'empty-meter')
            stop={'transactionId':tx,'meterStop':220,'timestamp':'2026-10-02T04:02:00Z'}
            if version=='1.5': stop['transactionData']=[{'values':[meter]}]
            await call('StopTransaction',stop,'same-stop')
            await call('StatusNotification',{'connectorId':1,'status':'Occupied','errorCode':'Mode3Error'},'same-status')
            for action,payload in [('Reset',{'type':'Soft'}),('UnlockConnector',{'connectorId':1})]:
                task=asyncio.create_task(asyncio.to_thread(api,port,f'/api/v1/chargepoints/CP_TEST/commands/{action}',tokens['operator'],payload))
                wire=json.loads(await asyncio.wait_for(ws.recv(),5))
                await ws.send(json.dumps([3,wire[1],{'status':'Accepted'}]))
                result=await task
                check(result==(200,{'status':'Unlocked' if action=='UnlockConnector' else 'Accepted'}),'legacy outgoing '+version+' '+action)
            if version=='1.5':
                payload={'listVersion':30,'updateType':'Full','localAuthorizationList':[{'idTag':'TAG','idTagInfo':{'status':'Accepted'}}]}
                task=asyncio.create_task(asyncio.to_thread(api,port,'/api/v1/chargepoints/CP_TEST/commands/SendLocalList',tokens['operator'],payload))
                wire=json.loads(await asyncio.wait_for(ws.recv(),5))
                check('localAuthorisationList' in wire[3] and 'localAuthorizationList' not in wire[3],'British legacy local list spelling')
                await ws.send(json.dumps([3,wire[1],{'status':'Accepted'}]))
                check((await task)[0]==200,'legacy local-list persistence')
        await asyncio.sleep(.1)
    with db.cursor() as cursor:
        cursor.execute("SELECT COUNT(DISTINCT protocol) FROM cpp_legacy_replay WHERE message_id='same-start'")
        check(cursor.fetchone()[0]==2,'replay identity includes protocol')

    def post(version,wire,auth=authorization,router=False):
        endpoint='CentralSystemService' if router else 'CentralSystemServiceOCPP'+version.replace('.','')
        request=urllib.request.Request(f'http://127.0.0.1:{port}/develop/services/'+endpoint,data=wire.encode(),headers={'Content-Type':'application/soap+xml; charset=utf-8','Authorization':auth})
        try: response=urllib.request.build_opener(urllib.request.ProxyHandler({})).open(request,timeout=5)
        except urllib.error.HTTPError as error: response=error
        return response.status,response.read()
    for version,year in [('1.2','2010/08'),('1.5','2012/06'),('1.6','2015/10')]:
        space='urn://Ocpp/Cs/'+year+'/'
        def envelope(body,identifier='urn:uuid:soap-message'):
            return f'<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:c="{space}" xmlns:a="http://www.w3.org/2005/08/addressing"><s:Header><c:chargeBoxIdentity>CP_TEST</c:chargeBoxIdentity><a:MessageID>{identifier}</a:MessageID></s:Header><s:Body>{body}</s:Body></s:Envelope>'
        boot=envelope('<c:bootNotificationRequest><c:chargePointVendor>test</c:chargePointVendor><c:chargePointModel>test</c:chargePointModel></c:bootNotificationRequest>')
        status,body=await asyncio.to_thread(post,version,boot)
        check(status==200,'SOAP boot '+version)
        tree=ET.fromstring(body)
        interval='interval' if version=='1.6' else 'heartbeatInterval'
        check(tree.find('.//{'+space+'}'+interval).text=='60','SOAP typed interval '+version)
        check(tree.find('.//{http://www.w3.org/2005/08/addressing}RelatesTo').text=='urn:uuid:soap-message','SOAP addressing '+version)
        heartbeat=envelope('<c:heartbeatRequest/>')
        check((await asyncio.to_thread(post,version,heartbeat))[0]==200,'SOAP heartbeat '+version)
        check((await asyncio.to_thread(post,version,heartbeat,authorization,True))[0]==200,'SOAP namespace router '+version)
        check((await asyncio.to_thread(post,version,envelope('<c:meterValuesRequest><c:connectorId>1</c:connectorId></c:meterValuesRequest>','empty-meter')))[0]==200,'SOAP empty meter notification '+version)
        status,body=await asyncio.to_thread(post,version,heartbeat,'')
        check(status==401,'SOAP authentication '+version)
        fault=ET.fromstring(body)
        soap='{http://www.w3.org/2003/05/soap-envelope}'
        check(fault.find('.//'+soap+'Code/'+soap+'Value').text=='s:Sender','SOAP authentication Sender fault '+version)
        check(fault.find('.//{http://www.w3.org/2005/08/addressing}RelatesTo').text=='urn:uuid:soap-message','SOAP fault correlation '+version)
        invalid=envelope('<c:authorizeRequest/>','invalid-schema-id')
        status,body=await asyncio.to_thread(post,version,invalid)
        check(status==400 and ET.fromstring(body).find('.//{http://www.w3.org/2005/08/addressing}RelatesTo').text=='invalid-schema-id','SOAP invalid payload fault correlation '+version)
        hostile='<!DOCTYPE s:Envelope [<!ENTITY ext SYSTEM "file:///etc/passwd">]>'+heartbeat
        status,body=await asyncio.to_thread(post,version,hostile)
        check(status==400,'SOAP DTD forbidden '+version)
        fault=ET.fromstring(body)
        check(fault.find('.//'+soap+'Reason/'+soap+'Text').attrib['{http://www.w3.org/XML/1998/namespace}lang']=='en','SOAP fault reason language '+version)
        check(fault.find('.//{http://www.w3.org/2005/08/addressing}RelatesTo') is None,'Malformed SOAP never reflects unchecked message ID '+version)
        duplicate=heartbeat.replace('<c:heartbeatRequest/>','<c:heartbeatRequest/><c:heartbeatRequest/>')
        check((await asyncio.to_thread(post,version,duplicate))[0]==400,'SOAP multiple body elements '+version)
