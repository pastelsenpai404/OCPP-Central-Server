"""Add a deterministic, linked sandbox dataset. Never deletes existing records or calls a bank.

Requires an existing management SQLite DB, creates an online backup, commits all rows
atomically and uses a seed manifest to make repeated runs harmless. Chargeboxes are
queued through the same persistent OCPP outbox as API-created records.
"""
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import random
import sqlite3


def seed(database, backup, days=30, random_seed=20261003):
    if not 7 <= days <= 90:
        raise ValueError('days must be 7–90')
    if not database.is_file():
        raise ValueError('Management DB must already exist; start the API first')
    connection = sqlite3.connect(database.resolve().as_uri() + '?mode=rw', uri=True, timeout=10)
    connection.row_factory = sqlite3.Row
    try:
        tables = {r[0] for r in connection.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        if not {'records','audit','invoice_sessions','ocpp_outbox'}.issubset(tables):
            raise ValueError('Unsupported management database; no data changed')
        manifest_key = 'ev-demo-v1'
        fingerprint = json.dumps({'days':days,'seed':random_seed},sort_keys=True)
        if 'demo_seed_runs' in tables:
            previous = connection.execute('SELECT fingerprint,summary FROM demo_seed_runs WHERE key=?',(manifest_key,)).fetchone()
            if previous:
                if previous['fingerprint'] != fingerprint:
                    raise ValueError('Demo already seeded with different options; refusing to replace data')
                return dict(json.loads(previous['summary']),already_seeded=True)
        if connection.execute("SELECT id FROM records WHERE json_extract(data,'$.code') LIKE 'DEMO-%' OR json_extract(data,'$.reference') LIKE 'DEMO-%' LIMIT 1").fetchone():
            raise ValueError('DEMO identifiers already exist without this seed manifest; refusing to overwrite')
        if backup.exists() or database.resolve() == backup.resolve():
            raise ValueError('Use a new backup path outside the database file')
        backup.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
        # Exclusive creation and private mode avoid overwriting or exposing a snapshot.
        backup.touch(mode=0o600,exist_ok=False)
        snapshot = sqlite3.connect(backup)
        try:
            connection.backup(snapshot)
            assert snapshot.execute('PRAGMA quick_check').fetchone()[0]=='ok'
        finally:
            snapshot.close()
        rng = random.Random(random_seed)
        now = datetime.now(timezone.utc).replace(microsecond=0)
        start = now.date()-timedelta(days=days-1)
        baseline=datetime.combine(start,datetime.min.time(),tzinfo=timezone.utc)
        counts = Counter()
        def stamp(value):
            return value.strftime('%Y-%m-%dT%H:%M:%SZ')
        def audit(action,kind,id,when):
            connection.execute('INSERT INTO audit(actor,action,kind,record_id,created_at) VALUES(?,?,?,?,?)',
                               ('demo-seeder',action,kind,id,stamp(when)))
        def insert(kind,data,when=None):
            when=when or baseline
            id=connection.execute('INSERT INTO records(kind,data,created_at,updated_at) VALUES(?,?,?,?)',
                                  (kind,json.dumps(data,ensure_ascii=False),stamp(when),stamp(when))).lastrowid
            counts[kind]+=1
            audit('demo-create',kind,id,when)
            return id
        connection.execute('BEGIN IMMEDIATE')
        try:
            companies=[insert('companies',{'code':f'DEMO-CO{i+1:02}','name':name+' (Demo)',
                'email':f'company{i+1}@example.invalid','phone':'0000000000','address':'Synthetic demo business address'})
                for i,name in enumerate(['Northstar Charge','Urban Volt'])]
            projects=[insert('projects',{'code':f'DEMO-PR{i+1:02}','name':name+' (Demo)','company_id':companies[i%2],'status':'active'})
                for i,name in enumerate(['Retail Network','City Residences','Highway Hubs','Workplace Charging'])]
            tariffs=[insert('tariffs',{'code':f'DEMO-TF{i+1:02}','name':name+' (Demo)','satang_per_kwh':rate,'vat_percent':7})
                for i,(name,rate) in enumerate([('AC Standard',550),('DC Fast',750)])]
            stations=[];chargers=[];connectors=[]
            locations=['Siam Square','Riverside Mall','Sukhumvit Residence','Ari Workplace','Bang Na Hub','Rangsit Plaza','Chaeng Watthana','Expressway Service Area']
            for i,location in enumerate(locations):
                station=insert('stations',{'code':f'DEMO-ST{i+1:02}','name':location+' (Demo)','project_id':projects[i%4],
                    'tariff_id':tariffs[i%2],'status':'active','address':location+' · Synthetic site','opening_hours':'06:00–23:00'},baseline)
                stations.append(station)
                for j in range(2):
                    index=i*2+j
                    code=f'DEMO-CB{index+1:03}'
                    charger=insert('chargers',{'code':code,'name':f'{location} Charger {j+1} (Demo)',
                        'station_id':station,'model':'Demo AC 22' if i%2==0 else 'Demo DC 120',
                        'protocol':'ocpp1.6' if index%3 else 'ocpp2.0.1','status':'available'},baseline)
                    chargers.append(charger)
                    connection.execute('INSERT INTO ocpp_outbox(charger_id,code) VALUES(?,?)',(charger,code))
                    for plug in range(2):
                        connector=insert('connectors',{'code':f'DEMO-CN{index+1:03}-{plug+1}','name':f'Connector {index+1}/{plug+1} (Demo)',
                            'charger_id':charger,'connector_number':plug+1,'power_kw':22 if i%2==0 else 120,
                            'type':'Type2' if i%2==0 else 'CCS2','status':'available'},baseline)
                        connectors.append({'id':connector,'station_id':station,'rate':550 if i%2==0 else 750})
            customers=[];balances={}
            given=['Alex','Jamie','Taylor','Morgan','Jordan','Casey','Riley','Avery','Charlie','Quinn']
            family=['Park','Lee','Rivera','Kim','Chen','Patel']
            for i in range(60):
                customer=insert('customers',{'code':f'DEMO-CU{i+1:03}','name':f'{given[i%10]} {family[i//10]} (Demo)',
                    'email':f'customer{i+1:03}@example.invalid','phone':'0000000000','address':'Synthetic customer address','status':'active'},baseline)
                customers.append(customer)
                balances[customer]=300000+rng.randrange(0,300001,10000)
                insert('wallet-events',{'customer_id':customer,'amount_satang':balances[customer],'balance_satang':balances[customer],
                    'type':'top-up','reference':f'DEMO-TOPUP-{i+1:03}','mode':'sandbox'},baseline)
            sessions=defaultdict(list)
            cycle_end=now.date()-timedelta(days=10)
            for day in range(days):
                date=start+timedelta(days=day)
                # Weekends and peak hours produce realistic variation; never create future events.
                daily_count=rng.randint(12,24) + (6 if date.weekday()>=5 else 0)
                for j in range(daily_count):
                    when=datetime.combine(date,datetime.min.time(),tzinfo=timezone.utc)+timedelta(hours=rng.randint(6,22),minutes=rng.randint(0,59))
                    when=min(when,now-timedelta(minutes=rng.randint(15,120)))
                    customer=rng.choice(customers);plug=rng.choice(connectors);energy=rng.randint(6000,54000)
                    subtotal=(energy*plug['rate']+500)//1000
                    id=insert('sessions',{'customer_id':customer,'connector_id':plug['id'],'station_id':plug['station_id'],
                        'energy_wh':energy,'satang_per_kwh':plug['rate'],'vat_percent':7,'subtotal_satang':subtotal,
                        'reference':f'DEMO-SESSION-{day+1:02}-{j+1:03}','status':'completed','mode':'sandbox'},when)
                    adjustment=0
                    if rng.random()<.06:
                        adjustment=rng.randint(100,min(2000,subtotal))
                        insert('refunds',{'session_id':id,'customer_id':customer,'amount_satang':adjustment,
                            'reason':'DEMO: interrupted charging compensation','mode':'sandbox'},min(when+timedelta(minutes=10),now))
                    sessions[customer].append({'id':id,'date':when.date(),'energy':energy,'net':subtotal-adjustment,'refund':adjustment})
            for index,customer in enumerate(customers):
                selected=[s for s in sessions[customer] if s['date']<=cycle_end]
                if not selected:
                    continue
                customer_data=json.loads(connection.execute('SELECT data FROM records WHERE id=?',(customer,)).fetchone()[0])
                lines=[{'session_id':s['id'],'energy_wh':s['energy'],'subtotal_satang':s['net'],
                    'refund_satang':s['refund'],'vat_satang':(s['net']*7+50)//100,'vat_percent':7} for s in selected]
                subtotal=sum(s['subtotal_satang'] for s in lines);vat=sum(s['vat_satang'] for s in lines)
                bill_data={'customer_id':customer,'customer_snapshot':customer_data,'period_from':start.isoformat(),
                    'period_to':cycle_end.isoformat(),'lines':lines,'subtotal_satang':subtotal,'vat_satang':vat,
                    'total_satang':subtotal+vat,'status':'issued','mode':'sandbox'}
                issued=datetime.combine(cycle_end+timedelta(days=1),datetime.min.time(),tzinfo=timezone.utc)+timedelta(hours=8)
                bill=insert('bills',bill_data,issued)
                connection.executemany('INSERT INTO invoice_sessions(session_id,bill_id) VALUES(?,?)',[(s['id'],bill) for s in selected])
                if index%4!=0:
                    method='wallet' if index%3 else 'manual'
                    paid=min(issued+timedelta(days=1,hours=index%8),now)
                    if method=='wallet' and balances[customer]>=bill_data['total_satang']:
                        balances[customer]-=bill_data['total_satang']
                        insert('wallet-events',{'customer_id':customer,'amount_satang':-bill_data['total_satang'],
                            'balance_satang':balances[customer],'type':'bill-payment','bill_id':bill,
                            'reference':f'DEMO-PAY-{bill}','mode':'sandbox'},paid)
                    else:
                        method='manual'
                    bill_data.update(status='paid',payment_method=method,payment_reference=f'DEMO-PAY-{bill}')
                    connection.execute('UPDATE records SET data=?,version=2,updated_at=? WHERE id=?',(json.dumps(bill_data),stamp(paid),bill))
                    audit('demo-pay','bills',bill,paid)
                    snapshot=dict(bill_data,id=bill,version=2,created_at=stamp(issued),updated_at=stamp(paid),deleted_at=None)
                    insert('tax-invoices',{'bill_id':bill,'number':f'TEST-TAX-{bill}','bill_snapshot':snapshot,
                        'customer_id':customer,'total_satang':subtotal+vat,'status':'issued','mode':'sandbox'},paid)
            for i,(kind,name) in enumerate([('reader','Monitoring team'),('operator','Operations team'),('admin','Workspace admin')]):
                insert('admins',{'code':f'DEMO-ADM{i+1}','name':name+' (Demo)','email':f'team{i+1}@example.invalid','role':kind,'status':'active'})
            for i,state in enumerate(['open','in_progress','resolved','open','resolved']):
                insert('maintenance',{'code':f'DEMO-JOB{i+1:02}','name':['Cooling inspection','Connector cable check','Firmware review','Meter calibration','Routine service'][i]+' (Demo)',
                    'station_id':stations[i],'assignee':'Operations team (Demo)','notes':'Synthetic maintenance scenario','status':state},now-timedelta(hours=i+1))
            for index,state in [(0,'charging'),(3,'faulted'),(8,'offline')]:
                connection.execute("UPDATE records SET data=json_set(data,'$.status',?),version=version+1 WHERE id=?",(state,chargers[index]))
            for index,state in [(0,'charging'),(6,'faulted')]:
                connection.execute("UPDATE records SET data=json_set(data,'$.status',?),version=version+1 WHERE id=?",(state,connectors[index]['id']))
            summary={'counts':dict(counts),'days':days,'dataset':manifest_key,'already_seeded':False}
            connection.execute('CREATE TABLE IF NOT EXISTS demo_seed_runs(key TEXT PRIMARY KEY,fingerprint TEXT NOT NULL,summary TEXT NOT NULL,created_at TEXT NOT NULL)')
            connection.execute('INSERT INTO demo_seed_runs(key,fingerprint,summary,created_at) VALUES(?,?,?,?)',(manifest_key,fingerprint,json.dumps(summary),stamp(now)))
            connection.commit()
            assert connection.execute('PRAGMA quick_check').fetchone()[0]=='ok'
            return summary
        except BaseException:
            connection.rollback()
            raise
    finally:
        connection.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--database',type=Path,required=True)
    parser.add_argument('--backup',type=Path,required=True)
    parser.add_argument('--days',type=int,default=30)
    parser.add_argument('--seed',type=int,default=20261003)
    args=parser.parse_args()
    print(json.dumps(seed(args.database,args.backup,args.days,args.seed),sort_keys=True))
