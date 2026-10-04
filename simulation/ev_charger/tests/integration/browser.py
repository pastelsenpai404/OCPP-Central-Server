"""Production static build: hydration/CSP, WASM and real mock-session UI controls."""
import os
from pathlib import Path
import secrets
import subprocess
import sys
import time
from playwright.sync_api import sync_playwright,expect
sys.path.insert(0,str(Path(__file__).parent))
from workflows import freeport

root=Path(__file__).resolve().parents[2]
port=freeport();token=secrets.token_hex(32)
env=os.environ.copy();env.update(SIM_PORT=str(port),SIM_API_HOST=f'127.0.0.1:{port}',SIM_PUBLIC_ORIGIN=f'http://127.0.0.1:{port}',SIM_ADMIN_TOKEN=token,SIM_WEB_ROOT=str(root/'frontend/dist'),SIM_CSMS_URL='',SIM_STATION_SECRETS='{}')
process=subprocess.Popen([str(Path(sys.argv[1]).resolve())],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
try:
    time.sleep(.5)
    with sync_playwright() as playwright:
        browser=playwright.chromium.launch(channel='msedge',headless=True)
        page=browser.new_page(viewport={'width':1440,'height':1100})
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
        page.on('console',lambda msg:errors.append(msg.text) if msg.type=='error' else None)
        page.goto(f'http://127.0.0.1:{port}/')
        page.get_by_label('Workspace token').fill(token)
        page.get_by_role('button',name='Connect →',exact=True).click()
        expect(page.get_by_role('heading',name='Charging lab',exact=True)).to_be_visible()
        expect(page.get_by_text('WASM ready',exact=False)).to_be_visible()
        page.get_by_role('button',name='Add station +').click()
        expect(page.get_by_role('heading',name='SIM01',exact=True)).to_be_visible()
        expect(page.get_by_text('Boot accepted',exact=False)).to_be_visible(timeout=8000)
        page.get_by_role('button',name='Normal session',exact=False).click()
        expect(page.locator('.vehicle .pill')).to_have_text('Charging',timeout=8000)
        page.get_by_role('button',name='Pause',exact=True).click()
        expect(page.locator('.vehicle .pill')).to_have_text('SuspendedEVSE',timeout=8000)
        page.get_by_role('button',name='Resume',exact=True).click()
        expect(page.locator('.vehicle .pill')).to_have_text('Charging',timeout=8000)
        page.get_by_role('button',name='Emergency stop',exact=False).click()
        expect(page.locator('.vehicle .pill')).to_have_text('Faulted',timeout=8000)
        out=root/'.deps/browser';out.mkdir(parents=True,exist_ok=True)
        page.screenshot(path=str(out/'lab-desktop.png'),full_page=True)
        page.set_viewport_size({'width':390,'height':844})
        assert page.evaluate('document.documentElement.scrollWidth<=window.innerWidth'), 'Mobile overflow'
        page.screenshot(path=str(out/'lab-mobile.png'),full_page=True)
        assert not errors,errors
        assert page.evaluate('localStorage.length===0 && sessionStorage.length===0')
        browser.close()
        print('Browser checks passed: production CSP/hydration/WASM, create/start/pause/resume/fault and mobile layout')
finally:
    process.terminate();process.wait(timeout=10)
