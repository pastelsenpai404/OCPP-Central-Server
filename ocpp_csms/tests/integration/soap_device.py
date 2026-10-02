"""Independent HTTP device fixture. Bound to ephemeral loopback port only."""
import base64
import threading
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
import xml.etree.ElementTree as ET

class SoapDevice:
    def __init__(self,passwords):
        self.requests=[]
        self.mode='normal'
        self.release=threading.Event()
        owner=self
        class Handler(BaseHTTPRequestHandler):
            def log_message(self,*args): pass
            def do_POST(self):
                version=self.path[1:]
                station='SOAP_'+version.replace('.','')
                expected='Basic '+base64.b64encode((station+':'+passwords[station]).encode()).decode()
                if self.headers.get('Authorization')!=expected:
                    self.send_error(401); return
                wire=self.rfile.read(int(self.headers['Content-Length']))
                root=ET.fromstring(wire)
                body=root.find('{http://www.w3.org/2003/05/soap-envelope}Body')
                action=body[0].tag.split('}')[1][:-7]
                action=action[0].upper()+action[1:]
                identifier=root.find('.//{http://www.w3.org/2005/08/addressing}MessageID').text
                owner.requests.append((station,action,wire))
                if owner.mode=='hold': owner.release.wait(25)
                if owner.mode=='redirect':
                    self.send_response(302); self.send_header('Location','http://127.0.0.1:1/private'); self.end_headers(); return
                space='urn://Ocpp/Cp/'+{'1.2':'2010/08','1.5':'2012/06','1.6':'2015/10'}[version]+'/'
                if owner.mode=='namespace': space='urn:foreign'
                envelope=ET.Element('{http://www.w3.org/2003/05/soap-envelope}Envelope')
                header=ET.SubElement(envelope,'{http://www.w3.org/2003/05/soap-envelope}Header')
                ET.SubElement(header,'{http://www.w3.org/2005/08/addressing}RelatesTo').text=identifier if owner.mode!='correlation' else 'urn:uuid:foreign'
                response_body=ET.SubElement(envelope,'{http://www.w3.org/2003/05/soap-envelope}Body')
                response=ET.SubElement(response_body,'{'+space+'}'+action[0].lower()+action[1:]+'Response')
                ET.SubElement(response,'{'+space+'}status').text='Unlocked' if action=='UnlockConnector' and version=='1.6' else 'Accepted'
                reply=ET.tostring(envelope,encoding='utf-8')
                try:
                    self.send_response(200); self.send_header('Content-Type','application/soap+xml'); self.send_header('Content-Length',str(len(reply))); self.end_headers(); self.wfile.write(reply)
                except (BrokenPipeError,ConnectionResetError): pass
        self.server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True)
    @property
    def origin(self): return 'http://127.0.0.1:'+str(self.server.server_port)
    def start(self): self.thread.start()
    def close(self): self.server.shutdown(); self.server.server_close(); self.thread.join(5)

async def outgoing_soap(port,tokens,device,api,check):
    import asyncio
    for version in ('1.2','1.5','1.6'):
        station='SOAP_'+version.replace('.','')
        for action,payload in [('Reset',{'type':'Soft'}),('UnlockConnector',{'connectorId':1})]:
            result=await asyncio.to_thread(api,port,f'/api/v1/chargepoints/{station}/commands/{action}',tokens['operator'],payload)
            check(result==(200,{'status':'Unlocked' if action=='UnlockConnector' else 'Accepted'}),'outgoing SOAP '+version+' '+action)
            check(device.requests[-1][0:2]==(station,action),'SOAP configured endpoint and action')
    for mode in ('namespace','correlation','redirect'):
        device.mode=mode
        result=await asyncio.to_thread(api,port,'/api/v1/chargepoints/SOAP_16/commands/Reset',tokens['operator'],{'type':'Soft'})
        check(result[0]==502,'hostile SOAP reply '+mode)
    device.mode='normal'
