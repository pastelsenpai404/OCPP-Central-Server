"""Simulated settlements; no card, account, gateway or actual payment fixtures."""
def sandbox(port,tokens,db,api,check):
    request={'caseId':'BILL_SANDBOX_1','currency':'THB','paidMinor':1000,'pricePerKwhMinor':800,
             'parkingPerMinuteMinor':20,'parkingGraceSeconds':60,
             'meterStart':{'value':'10.000000','unit':'kWh'},'meterStop':{'value':'11000','unit':'Wh'},
             'chargeEndedAt':'2026-10-02T01:00:00Z','departedAt':'2026-10-02T09:01:00.000001+08:00'}
    path='/api/v1/billing/sandbox/settle'
    check(api(port,path,tokens['operator'],request)[0]==401,'only administrator can create sandbox settlement')
    response=api(port,path,tokens['admin'],request)
    check(response[0]==200 and response[1]['energyCostMinor']==800 and response[1]['parkingCostMinor']==20 and response[1]['refundMinor']==180 and response[1]['paymentExecuted']==False,'sandbox money and mixed meter units')
    check(api(port,path,tokens['admin'],request)==response,'settlement idempotency')
    changed=dict(request,paidMinor=1500)
    check(api(port,path,tokens['admin'],changed)[0]==400,'conflicting settlement is rejected')
    with db.cursor() as cursor:
        cursor.execute('SELECT COUNT(*) FROM cpp_billing_sandbox WHERE case_id=%s',(request['caseId'],))
        check(cursor.fetchone()[0]==1,'single durable simulated settlement')
    check(api(port,'/api/v1/billingSandbox',tokens['read'])[0]==401,'sandbox financial data authorization')
    check(len(api(port,'/api/v1/billingSandbox',tokens['admin'])[1])==1,'sandbox ledger overview')
