"""Generate bounded JSON validation/typed XML metadata from the pinned legacy WSDLs.
WSDL originals remain unmodified. Not used at server runtime.
"""
import argparse
import copy
import json
from pathlib import Path
import xml.etree.ElementTree as ET

X='{http://www.w3.org/2001/XMLSchema}'
ROOT=Path(__file__).resolve().parents[1]
FILES={
    '1.2':['centralsystemservice_0.wsdl','chargepointservice.wsdl'],
    '1.5':['ocpp_centralsystemservice_1.5_final.wsdl','ocpp_chargepointservice_1.5_final.wsdl'],
    '1.6':['OCPP_CentralSystemService_1.6.wsdl','OCPP_ChargePointService_1.6.wsdl']}

def generate(source):
    for version,names in FILES.items():
        definitions={}; elements={}; namespaces={}; xml_fields={}
        def type_schema(name):
            local=name.split(':')[-1]
            if ':' in name and name.split(':')[0] in ('s','xs','xsd'):
                if local in ('int','integer','nonNegativeInteger','unsignedInt'): return {'type':'integer','minimum':0 if local.startswith(('nonNegative','unsigned')) else -2147483648,'maximum':2147483647}
                if local in ('decimal','double','float'): return {'type':'number'}
                if local=='boolean': return {'type':'boolean'}
                if local=='dateTime': return {'type':'string','format':'date-time'}
                if local in ('string','anyURI'): return {'type':'string','maxLength':4096}
                raise ValueError('Unsupported primitive '+name)
            return {'$ref':'#/definitions/'+local}
        def complex_schema(node):
            properties={}; required=[]; metadata=[]
            extension=node.find(X+'simpleContent/'+X+'extension')
            if extension is not None:
                properties['value']=type_schema(extension.attrib['base']); required.append('value')
                metadata.append({'name':'value','text':True})
                for attribute in extension.findall(X+'attribute'):
                    name=attribute.attrib['name']; properties[name]=type_schema(attribute.attrib['type'])
                    metadata.append({'name':name,'attribute':True})
                    if attribute.get('use')=='required': required.append(name)
            sequence=node.find(X+'sequence')
            if sequence is not None:
                for element in sequence.findall(X+'element'):
                    name=element.attrib['name']
                    schema=type_schema(element.attrib['type']) if 'type' in element.attrib else complex_schema(element.find(X+'complexType'))[0]
                    minimum=int(element.get('minOccurs','1')); maximum=element.get('maxOccurs','1')
                    item_metadata={'name':name,'schema':copy.deepcopy(schema)}
                    if maximum!='1':
                        schema={'type':'array','items':schema,'minItems':minimum,'maxItems':min(512,int(maximum)) if maximum!='unbounded' else 512}
                        item_metadata['array']=True
                    properties[name]=schema; metadata.append(item_metadata)
                    if minimum: required.append(name)
            result={'type':'object','properties':properties,'additionalProperties':False}
            if extension is not None: result['x-xmlFields']=metadata
            if required: result['required']=required
            return result,metadata
        for index,filename in enumerate(names):
            document=ET.parse(source/filename)
            schema=document.find('.//'+X+'schema')
            namespaces['cs' if index==0 else 'cp']=schema.attrib['targetNamespace']
            for node in schema:
                name=node.get('name')
                if not name: continue
                if node.tag==X+'simpleType':
                    restriction=node.find(X+'restriction'); generated=type_schema(restriction.attrib['base'])
                    values=[e.attrib['value'] for e in restriction.findall(X+'enumeration')]
                    if values: generated['enum']=values
                    for field,keyword in [('maxLength','maxLength'),('minLength','minLength'),('minInclusive','minimum'),('maxInclusive','maximum')]:
                        limit=restriction.find(X+field)
                        if limit is not None: generated[keyword]=int(limit.attrib['value'])
                    definitions[name]=generated
                elif node.tag==X+'complexType':
                    definitions[name],xml_fields[name]=complex_schema(node)
                elif node.tag==X+'element' and node.get('type'):
                    elements[name]=node.attrib['type'].split(':')[-1]
            originals=ROOT/'protocols/wsdl'; originals.mkdir(parents=True,exist_ok=True)
            (originals/filename).write_bytes((source/filename).read_bytes())
        destination=ROOT/'schemas'/version; destination.mkdir(parents=True,exist_ok=True)
        count=0
        for name,definition in definitions.items():
            if not name.endswith(('Request','Response')): continue
            filename=name[:-7] if name.endswith('Request') else name
            output=copy.deepcopy(definition); output['definitions']=definitions
            (destination/(filename+'.json')).write_text(json.dumps(output,ensure_ascii=True,indent=2)+'\n',encoding='utf-8')
            count+=1
        (destination/'soap.registry').write_text(json.dumps({'definitions':definitions,'elements':elements,'namespaces':namespaces,'xmlFields':xml_fields},indent=2)+'\n',encoding='utf-8')
        print(version,count,'request/response schemas')

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('source',type=Path)
    generate(parser.parse_args().source)
