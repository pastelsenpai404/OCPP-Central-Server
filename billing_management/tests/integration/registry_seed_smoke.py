"""Isolated durable registry integration and deterministic sandbox ledger checks."""
import argparse
from contextlib import closing
from datetime import datetime, timezone
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import json
import os
from pathlib import Path
import secrets
import socket
import sqlite3
import subprocess
import tempfile
import threading
import time


def test(binary):
    spec = importlib.util.spec_from_file_location('demo_seed', Path(__file__).parents[2] / 'scripts/seed-demo.py')
    seeder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(seeder)
    with tempfile.TemporaryDirectory() as directory:
        database = Path(directory) / 'sandbox.sqlite3'
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            port = listener.getsockname()[1]
        admin, reader, bridge = [secrets.token_hex(32) for _ in range(3)]
        requests = []
        remote_status = 503
        class Registry(BaseHTTPRequestHandler):
            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                assert self.path == '/api/v1/admin/chargepoints/register'
                assert self.headers['Host'] == 'ocpp.test.invalid'
                assert self.headers['Authorization'] == 'Bearer ' + bridge
                assert set(body) == {'chargeBoxId'}
                requests.append(body['chargeBoxId'])
                self.send_response(remote_status)
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps(dict(body, registryOnly=True, credentialsConfigured=False,
                                                 error='REMOTE-SECRET-MUST-NOT-LEAK')).encode())
            def log_message(self, *_):
                pass
        remote = ThreadingHTTPServer(('127.0.0.1', 0), Registry)
        thread = threading.Thread(target=remote.serve_forever, daemon=True)
        thread.start()
        env = dict(os.environ, BILLING_PORT=str(port), BILLING_API_HOST=f'127.0.0.1:{port}',
                   BILLING_API_TOKEN=admin, BILLING_READER_TOKEN=reader,
                   BILLING_OPERATOR_TOKEN=secrets.token_hex(32), BILLING_DATABASE_PATH=str(database),
                   BILLING_OCPP_TOKEN=bridge, BILLING_OCPP_HOST='ocpp.test.invalid',
                   BILLING_OCPP_PORT=str(remote.server_port))
        process = None
        def call(path, method='GET', data=None, token=admin, status=200):
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=10)
            connection.request(method, '/api/v1/management' + path,
                               json.dumps(data) if data is not None else None,
                               {'Authorization':'Bearer '+token,'Content-Type':'application/json'})
            response = connection.getresponse()
            body = response.read()
            connection.close()
            assert response.status == status, (path, response.status, body[:200])
            return json.loads(body)
        def start():
            nonlocal process
            process = subprocess.Popen([str(binary)], env=env, cwd=directory,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for _ in range(100):
                assert process.poll() is None, 'Server exited'
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
        def wait_status(id, state):
            for _ in range(160):
                row = call(f'/chargers/{id}')
                if row['ocpp_sync_status'] == state:
                    return row
                time.sleep(.1)
            raise AssertionError(('Sync timeout',state,row))
        try:
            start()
            company = call('/companies', 'POST', {'code':'EXISTING','name':'Existing record'})
            project = call('/projects', 'POST', {'code':'PR','name':'Project','company_id':company['id'],'status':'active'})
            tariff = call('/tariffs', 'POST', {'code':'TF','name':'Tariff','satang_per_kwh':750,'vat_percent':7})
            station = call('/stations', 'POST', {'code':'ST','name':'Station','project_id':project['id'],'tariff_id':tariff['id'],'status':'active'})
            data = {'code':'CB-TEST','name':'Charger','station_id':station['id'],'protocol':'ocpp1.6','status':'available'}
            charger = call('/chargers', 'POST', data)
            failed = wait_status(charger['id'], 'retrying')
            assert failed['ocpp_last_error'] == 'csms_rejected'
            assert 'REMOTE-SECRET' not in json.dumps(failed)
            call(f"/chargers/{charger['id']}/sync", 'POST', {}, reader, 403)
            call(f"/chargers/{charger['id']}", 'PATCH', dict(data,code='CHANGED',version=charger['version']),status=409)
            stop()
            remote_status = 201
            start()
            synced = wait_status(charger['id'], 'synced')
            assert not synced['ocpp_credentials_configured']
            count = len(requests)
            stop()
            start()
            time.sleep(2.3)
            assert len(requests) == count, 'Synced jobs replayed after restart'
            call(f"/chargers/{charger['id']}/sync", 'POST', {})
            wait_status(charger['id'], 'synced')
            assert requests == ['CB-TEST'] * len(requests)
            stop()  # Seed checks are independent of the running integration worker.
            summary = seeder.seed(database, Path(directory)/'before.sqlite3')
            assert summary['counts']['chargers'] == 16
            assert summary['counts']['customers'] == 60
            assert summary['counts']['sessions'] > 450
            with closing(sqlite3.connect(database)) as connection:
                rows = connection.execute('SELECT id,kind,data,created_at FROM records').fetchall()
                ledger = {}
                statuses = set()
                now = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
                for id,kind,text,created in rows:
                    data = json.loads(text)
                    assert created <= now
                    if kind == 'wallet-events':
                        customer = data['customer_id']
                        ledger[customer] = ledger.get(customer,0) + data['amount_satang']
                        assert ledger[customer] == data['balance_satang'] >= 0
                    if kind == 'bills':
                        statuses.add(data['status'])
                        assert sum(line['subtotal_satang'] for line in data['lines']) == data['subtotal_satang']
                        assert sum(line['vat_satang'] for line in data['lines']) == data['vat_satang']
                        assert data['subtotal_satang']+data['vat_satang'] == data['total_satang']
                    if kind == 'customers':
                        assert data['email'].endswith('@example.invalid')
                assert statuses == {'paid','issued'}
                assert connection.execute('PRAGMA quick_check').fetchone()[0] == 'ok'
                assert connection.execute('SELECT count(*) FROM ocpp_outbox').fetchone()[0] == 17
                assert json.loads(connection.execute('SELECT data FROM records WHERE id=?',(company['id'],)).fetchone()[0])['code'] == 'EXISTING'
                before = len(rows)
            assert seeder.seed(database, Path(directory)/'unused.sqlite3')['already_seeded']
            assert not (Path(directory)/'unused.sqlite3').exists()
            with closing(sqlite3.connect(database)) as connection:
                assert connection.execute('SELECT count(*) FROM records').fetchone()[0] == before
            with closing(sqlite3.connect(Path(directory)/'before.sqlite3')) as snapshot:
                assert snapshot.execute('SELECT count(*) FROM records').fetchone()[0] == 5
            print('Registry retry/restart/authentication and demo seed/ledger/backup checks passed')
        finally:
            stop()
            remote.shutdown()
            remote.server_close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,required=True)
    test(parser.parse_args().binary.resolve())
