const fs=require('fs');const t=fs.readFileSync(process.argv[2],'utf8');
const pl=t.slice(t.indexOf('<edc:PinList'),t.indexOf('</edc:PinList>'));
const pins=[...pl.matchAll(/<edc:Pin>([\s\S]*?)<\/edc:Pin>/g)].map(m=>[...m[1].matchAll(/edc:name="([^"]+)"/g)].map(x=>x[1]).filter(n=>!/^(IOC|RPO)/.test(n)));
pins.forEach((p,i)=>console.log(i+1, p.join('/')));
