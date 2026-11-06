#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""Tables of sweep.sh's runs: report.py RUNS TAG [TAG...]"""
import json,os,sys,re
R=sys.argv[1]
def load(n):
    try: return json.load(open(f'{R}/{n}/report.json'))
    except Exception as e: return None
def crates(d):
    n=0
    for c in d['checks']:
        if 'crates agree' in c['name'] and not c['ok']:
            try: n+=len(json.loads(c['detail']))
            except: n+=1
    return n
def ok(d):
    return sum(1 for c in d['checks'] if c['ok']),len(d['checks'])
def state(d):
    net=d.get('net',{})
    host=net.get('Host1',{})
    s=host.get('seconds',1)
    up=sum(v for k,v in host.get('count',{}).items() if k.startswith('udp/up/') and 'bind' not in k)
    ws=sum(v for k,v in host.get('count',{}).items() if k in('udp/up/9',))
    wr=sum(v.get('count',{}).get('udp/down/9',0) for n,v in net.items() if n!='Host1')
    return round(up/s,1),ws,wr
print('run (checks passed, of) crates_wrong (place, hidden, compared) [join s] | host datagrams/s, WORLD sent, WORLD received, failures')
for tag in sys.argv[2:]:
  for l in (0,1,5,15):
    for m in (30,100,250):
        n=f'{tag}-l{l}-m{m}'
        d=load(n)
        if d is None: print(n,'no report'); continue
        o=ok(d); p=d['notes'].get('poses',{})
        pp=[p[k] for k in p]
        t=sum(x['place'] for x in pp),sum(x['hidden'] for x in pp),sum(x['compared'] for x in pp)
        js=[c['detail'][:5] for c in d['checks'] if 'in control in the host' in c['name']]
        f=[c['detail'][:70] for c in d['checks'] if not c['ok'] and 'crates agree' not in c['name']]
        print(n,o,crates(d),t,js,state(d),f)
for n in [t+'-soak3' for t in sys.argv[2:]]:
    d=load(n)
    if not d: continue
    print(n,ok(d),d['server'],d['notes'].get('poses'))
    for who,v in d['net'].items():
        s=v['seconds']
        agg={}
        for k,c in v['bytes'].items():
            agg[k]=round(c/s/1000,2)
        print(' ',who,round(s),'kB/s',{k:x for k,x in sorted(agg.items()) if x>=0.05},'msg/s',{k:round(c/s,1) for k,c in sorted(v['count'].items()) if c/s>=1})
    for who,c in d.get('counts',{}).items():
        print(' ',who,{k:c[k] for k in ('data_frames','state_datagrams','state_held','state_tcp','waited','queue_most')})
