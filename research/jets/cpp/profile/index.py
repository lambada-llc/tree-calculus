import re,glob,os,sys
d='work/arb-cmdlog'
rows=[]
for sf in glob.glob(d+'/*.stats'):
    pid=os.path.basename(sf).split('.')[0]
    for line in open(sf):
        p=line.split()
        seq=p[0]; ms=float(p[1]); kv=dict(x.split('=') for x in p[2:9])
        cmd=line.split('|',1)[1].strip()
        name=''
        pay=f'{d}/{pid}-{seq}.payload'
        if os.path.exists(pay) and cmd.startswith('reduce'):
            t=open(pay,'rb').read().decode(errors='replace')
            m=re.findall(r'(:test\.[A-Za-z0-9_.]+)',t)
            name=m[0] if m else ''
        if cmd.startswith('load'): name='LOAD'
        rows.append((pid,int(seq),int(kv['steps']),ms,int(kv['gcs']),name,cmd[:30]))
rows.sort(key=lambda r:-r[2])
with open('t2/index.tsv','w') as f:
    for r in rows: f.write('\t'.join(map(str,r))+'\n')
