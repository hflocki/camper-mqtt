// usage: node render.js in.svg out.png [width]
const {chromium}=require('playwright');const fs=require('fs');
(async()=>{const [,,i,o,w]=process.argv;const svg=fs.readFileSync(i,'utf8');
const b=await chromium.launch();const p=await b.newPage({viewport:{width:+(w||1400),height:100}});
await p.setContent(`<html><body style="margin:0;background:#fff"><div id=c style="width:${w||1400}px">${svg}</div></body></html>`);
await p.evaluate(()=>{const s=document.querySelector('svg');s.setAttribute('width','100%');s.removeAttribute('height')});
await p.locator('#c').screenshot({path:o});await b.close()})();
