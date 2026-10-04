"""Real static SvelteKit UI, isolated C++ API/DB, desktop/mobile and WASM."""
import argparse
import functools
import http.server
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
import threading
import urllib.request
from playwright.sync_api import sync_playwright, expect


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass


def test(binary):
    root=Path(__file__).resolve().parents[2]
    artifacts=root/'.deps/backoffice-browser'
    artifacts.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory() as directory:
        static=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(QuietHandler,directory=str(root/'management/frontend/dist')))
        thread=threading.Thread(target=static.serve_forever,daemon=True)
        thread.start()
        with socket.socket() as s:
            s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        frontend=f'http://127.0.0.1:{static.server_port}'
        endpoint=f'http://127.0.0.1:{port}'
        token=secrets.token_hex(32)
        env=dict(os.environ,BILLING_PORT=str(port),BILLING_API_HOST=f'127.0.0.1:{port}',
                 BILLING_ALLOWED_ORIGIN=frontend,BILLING_API_TOKEN=token,
                 BILLING_DATABASE_PATH=str(Path(directory)/'ui.sqlite3'))
        process=subprocess.Popen([str(binary)],env=env,cwd=directory,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            with sync_playwright() as pw:
                browser=pw.chromium.launch(channel='msedge',headless=True)
                page=browser.new_page(viewport={'width':1440,'height':1000})
                errors=[]
                page.on('pageerror',lambda e:errors.append(str(e)))
                page.goto(frontend)
                page.get_by_label('API endpoint').fill(endpoint)
                page.get_by_label('Access token').fill(token)
                page.get_by_role('button',name='เข้าสู่ workspace →').click()
                expect(page.locator('h1')).to_have_text('ภาพรวม')
                expect(page.locator('.loading-state')).to_have_count(0)
                page.screenshot(path=str(artifacts/'dashboard-desktop.png'),full_page=True)
                page.locator('nav').get_by_role('button',name='บริษัท',exact=True).click()
                page.get_by_role('button',name='＋ เพิ่มบริษัท').click()
                dialog=page.get_by_role('dialog')
                dialog.get_by_label('รหัส *',exact=True).fill('UI_COMPANY')
                dialog.get_by_label('ชื่อบริษัท *').fill('UI Company')
                dialog.get_by_role('button',name='บันทึกข้อมูล').click()
                expect(dialog).to_have_count(0)
                expect(page.get_by_role('cell',name='UI Company',exact=True)).to_be_visible()
                page.get_by_role('button',name='แก้ไข',exact=True).click()
                dialog=page.get_by_role('dialog')
                dialog.get_by_label('ชื่อบริษัท *').fill('Updated company')
                dialog.get_by_role('button',name='บันทึกข้อมูล').click()
                expect(page.get_by_role('cell',name='Updated company',exact=True)).to_be_visible()
                page.get_by_role('button',name='รายละเอียด',exact=True).click()
                expect(page.get_by_role('dialog')).to_be_visible()
                page.keyboard.press('Escape')
                expect(page.get_by_role('dialog')).to_have_count(0)
                page.get_by_role('button',name='เก็บเข้าถังขยะ',exact=True).click()
                page.get_by_role('dialog').get_by_role('button',name='ยืนยัน',exact=True).click()
                expect(page.get_by_role('cell',name='Updated company',exact=True)).to_have_count(0)
                page.locator('nav').get_by_role('button',name='ถังขยะ',exact=True).click()
                page.get_by_role('button',name='กู้คืน',exact=True).click()
                expect(page.get_by_role('button',name='กู้คืน',exact=True)).to_have_count(0)
                page.locator('nav').get_by_role('button',name='ลูกค้า',exact=True).click()
                page.get_by_role('button',name='＋ เพิ่มลูกค้า').click()
                dialog=page.get_by_role('dialog')
                dialog.get_by_label('รหัส *',exact=True).fill('UI_CUSTOMER')
                dialog.get_by_label('ชื่อลูกค้า *').fill('UI Customer')
                dialog.get_by_role('button',name='บันทึกข้อมูล').click()
                expect(dialog).to_have_count(0)
                page.locator('nav').get_by_role('button',name='กระเป๋าเงิน',exact=True).click()
                page.get_by_role('button',name='เติมเงิน',exact=True).click()
                dialog=page.get_by_role('dialog')
                expect(dialog).to_be_visible()
                dialog.get_by_label('ลูกค้า',exact=True).select_option(label='UI Customer · UI_CUSTOMER')
                dialog.get_by_label('จำนวนเงิน (สตางค์)').fill('10000')
                dialog.get_by_label('เลขอ้างอิงทดสอบ').fill('UI top-up')
                dialog.get_by_role('button',name='ยืนยันรายการทดสอบ').click()
                expect(dialog).to_have_count(0)
                page.get_by_label('Customer wallet',exact=True).select_option(label='UI Customer · UI_CUSTOMER')
                expect(page.locator('.wallet-balance')).to_contain_text('100.00')
                page.locator('nav').get_by_role('button',name='เครื่องคำนวณ WASM',exact=True).click()
                expect(page.locator('output')).to_be_visible()
                expect(page.locator('.calculator-wrap button')).to_be_enabled()
                page.locator('nav').get_by_role('button',name='ภาพรวม',exact=True).click()
                page.set_viewport_size({'width':390,'height':844})
                expect(page.locator('h1')).to_have_text('ภาพรวม')
                assert page.evaluate('document.documentElement.scrollWidth <= window.innerWidth')
                page.screenshot(path=str(artifacts/'dashboard-mobile.png'),full_page=True)
                assert not errors,errors
                browser.close()
                print('Backoffice UI: login, CRUD, archive/restore, wallet, WASM and mobile passed')
        finally:
            process.terminate();process.wait(timeout=10)
            static.shutdown();static.server_close()


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,required=True)
    test(parser.parse_args().binary.resolve())
