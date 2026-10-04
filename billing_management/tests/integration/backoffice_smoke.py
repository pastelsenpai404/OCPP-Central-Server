"""Isolated management DB: business invariants, authorization, concurrency and restart."""
import argparse
import concurrent.futures
from datetime import datetime, timezone
import http.client
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
import time


def test(binary):
    with tempfile.TemporaryDirectory() as directory:
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            port = listener.getsockname()[1]
        admin, reader, operator = [secrets.token_hex(32) for _ in range(3)]
        env = dict(os.environ, BILLING_PORT=str(port), BILLING_API_HOST=f'127.0.0.1:{port}',
                   BILLING_ALLOWED_ORIGIN='http://127.0.0.1:5100', BILLING_API_TOKEN=admin,
                   BILLING_READER_TOKEN=reader, BILLING_OPERATOR_TOKEN=operator,
                   BILLING_DATABASE_PATH=str(Path(directory) / 'test.sqlite3'))
        process = None
        checks = 0

        def call(path, method='GET', data=None, token=admin, key=None, status=200, raw=None, extra=None):
            nonlocal checks
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
            headers = {'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json'}
            if key:
                headers['Idempotency-Key'] = key
            headers.update(extra or {})
            connection.request(method, '/api/v1/management' + path,
                               raw if raw is not None else json.dumps(data) if data is not None else None, headers)
            response = connection.getresponse()
            body = response.read()
            connection.close()
            assert response.status == status, (method, path, response.status, body[:300])
            checks += 1
            return json.loads(body) if body else None

        def start():
            nonlocal process
            process = subprocess.Popen([str(binary)], env=env, cwd=directory,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for _ in range(100):
                if process.poll() is not None:
                    raise AssertionError('Management server failed to start')
                try:
                    call('/me')
                    return
                except OSError:
                    time.sleep(.05)
            raise AssertionError('Readiness timeout')

        def stop():
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=10)

        def create(kind, data):
            return call('/' + kind, 'POST', data)

        def financial(path, data, key=None, status=200):
            return call(path, 'POST', data, key=key or secrets.token_hex(16), status=status)

        try:
            start()
            call('/me', token='invalid', status=401)
            assert call('/me', token=reader)['role'] == 'reader'
            call('/companies', 'POST', {}, token=reader, status=403)
            call('/admins', 'POST', {}, token=operator, status=403)
            call('/companies', extra={'Origin':'https://evil.example'}, status=403)
            call('/companies', extra={'Host':'evil.example'}, status=400)
            call('/sessions', 'POST', {}, status=400)
            call('/companies', 'POST', raw='{"code":"A","code":"B","name":"x"}', status=400)
            call('/companies', 'POST', {'code':'A','name':'x','secret':'rejected'}, status=400)
            call('/companies', 'POST', {'code':'B','name':'x'*5000}, status=413)
            call('/companies', 'POST', {'code':'A','name':['wrong']}, status=400)
            company=create('companies', {'code':'CO','name':"O'Reilly; DROP TABLE records; --"})
            call('/companies', 'POST', {'code':'CO','name':'Duplicate'}, status=409)
            project=create('projects', {'code':'PR','name':'Project','company_id':company['id'],'status':'active'})
            tariff=create('tariffs', {'code':'T','name':'Tariff','satang_per_kwh':750,'vat_percent':7})
            station=create('stations', {'code':'ST','name':'Station','project_id':project['id'],'tariff_id':tariff['id'],'status':'active'})
            charger=create('chargers', {'code':'CH','name':'Charger','station_id':station['id'],'protocol':'ocpp1.6','status':'available'})
            connector=create('connectors', {'code':'CN','name':'CCS','charger_id':charger['id'],'connector_number':1,'power_kw':120,'type':'CCS2','status':'available'})
            customer=create('customers', {'code':'CU','name':'Customer','status':'active'})
            call('/connectors','POST',{'code':'CN2','name':'Duplicate plug','charger_id':charger['id'],'connector_number':1,'power_kw':120,'type':'CCS2','status':'available'},status=409)
            call(f"/companies/{company['id']}",'DELETE',{'version':1},status=409)
            edited=dict(code='T',name='Tariff',satang_per_kwh=750,vat_percent=7,version=1)
            updated=call(f"/tariffs/{tariff['id']}",'PATCH',edited)
            assert updated['version']==2
            call(f"/tariffs/{tariff['id']}",'PATCH',edited,status=409)
            customer_id=customer['id']
            deposit={'customer_id':customer_id,'amount_satang':10000,'reference':'Bank sandbox'}
            key=secrets.token_hex(16)
            first=financial('/wallet/top-up',deposit,key)
            assert financial('/wallet/top-up',deposit,key)==first
            financial('/wallet/top-up',dict(deposit,amount_satang=10001),key,status=409)
            financial('/wallet/withdraw',dict(deposit,amount_satang=10001),status=409)
            assert call(f'/wallet/{customer_id}')['balance_satang']==10000
            session=financial('/sessions',{'customer_id':customer_id,'connector_id':connector['id'],'energy_wh':10001,'reference':'Session sandbox'})
            assert session['subtotal_satang']==7501
            financial(f"/sessions/{session['id']}/refund",{'amount_satang':7502,'reason':'Too much'},status=409)
            financial(f"/sessions/{session['id']}/refund",{'amount_satang':501,'reason':'Adjustment'})
            assert call(f'/wallet/{customer_id}')['balance_satang']==10000, 'Postpaid adjustment must not double-credit wallet'
            day=datetime.now(timezone.utc).date().isoformat()
            bill=financial('/bills',{'customer_id':customer_id,'period_from':day,'period_to':day})
            assert (bill['subtotal_satang'],bill['vat_satang'],bill['total_satang'])==(7000,490,7490)
            financial('/bills',{'customer_id':customer_id,'period_from':day,'period_to':day},status=409)
            financial('/bills',{'customer_id':customer_id,'period_from':'2026-02-30','period_to':day},status=400)
            financial(f"/sessions/{session['id']}/refund",{'amount_satang':1,'reason':'Already billed'},status=409)
            financial(f"/bills/{bill['id']}/issue-tax",{},status=409)
            payment={'method':'wallet','reference':'Sandbox pay'}
            key=secrets.token_hex(16)
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                results=list(pool.map(lambda _:financial(f"/bills/{bill['id']}/pay",payment,key),range(2)))
            assert results[0]==results[1]
            assert call(f'/wallet/{customer_id}')['balance_satang']==2510
            financial(f"/bills/{bill['id']}/pay",payment,status=409)
            invoice=financial(f"/bills/{bill['id']}/issue-tax",{})
            assert financial(f"/bills/{bill['id']}/issue-tax",{})['id']==invoice['id']
            call(f"/bills/{bill['id']}",'PATCH',{'status':'issued'},status=405)
            call(f'/customers/{customer_id}','DELETE',{'version':1},status=409)
            spare=create('maintenance',{'code':'M','name':'Maintenance','station_id':station['id'],'status':'open'})
            call(f"/maintenance/{spare['id']}",'DELETE',{'version':1})
            assert any(r['id']==spare['id'] for r in call('/trash')['items'])
            call(f"/maintenance/{spare['id']}/restore",'POST',{'version':2})
            assert call(f"/maintenance/{spare['id']}")['version']==3
            assert call('/companies?q=DROP')['total']==1
            call('/companies?offset=bad',status=400)
            assert call('/audit')['total']>=15
            stop();start()
            assert call(f'/wallet/{customer_id}')['balance_satang']==2510
            assert call(f"/bills/{bill['id']}")['status']=='paid'
            assert call(f"/tax-invoices/{invoice['id']}")['total_satang']==7490
            print(f'Backoffice: {checks} isolated API checks passed, including restart and concurrent payment')
        finally:
            stop()


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,required=True)
    test(parser.parse_args().binary.resolve())
