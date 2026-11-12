const fs=require('fs');const [file,name]=process.argv.slice(2);const t=fs.readFileSync(file,'utf8');
const i=t.indexOf(`(symbol "${name}"`);let d=0,j=i;for(;j<t.length;j++){if(t[j]=='(')d++;else if(t[j]==')'){d--;if(!d)break;}}
const s=t.slice(i,j+1);const ext=s.match(/\(extends "([^"]+)"/);if(ext)console.log('EXTENDS',ext[1]);
for(const m of s.matchAll(/\(pin (\w+) \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)\s*\(length ([\d.]+)\)[\s\S]*?\(name "([^"]*)"[\s\S]*?\(number "([^"]*)"/g))console.log(m[7],m[6],m[1],'at',m[2],m[3],'rot',m[4],'len',m[5]);
