#!/usr/bin/env python3
import datetime
import json
import os

import duckdb
import sys, time
T0=time.time()
LAST=[T0]
def lap(name):
    now=time.time()
    print(f"{name}: +{now-LAST[0]:.1f}s (total {now-T0:.1f}s)",file=sys.stderr)
    LAST[0]=now

DATA=os.environ.get("DATA_DIR","/data")
OUT=os.environ.get("OUTPUT_DIR","/output")
SCRATCH=os.environ.get("SCRATCH_DIR","/scratch")
NCPU=int(os.environ.get("NCPU",os.cpu_count() or 1))
MEM_GB=int(os.environ.get("MEM_GB","8"))

con=duckdb.connect()
con.execute(f"SET threads={NCPU}")
con.execute(f"SET memory_limit='{max(1,int(MEM_GB*0.8))}GB'")
con.execute(f"SET temp_directory='{os.path.join(SCRATCH,'duckdb_tmp')}'")
con.execute("SET preserve_insertion_order=false")

with open(os.path.join(DATA,"manifest.json")) as f:
    manifest=json.load(f)

def iso_ms(s):
    dt=datetime.datetime.strptime(s,"%Y-%m-%dT%H:%M:%SZ")
    return int(dt.replace(tzinfo=datetime.timezone.utc).timestamp())*1000

WS=iso_ms(manifest["window_start"])
WE=iso_ms(manifest["window_end"])
DAY=86400000
WEEK=7*DAY

# ---------- macros ----------
PAT=r"^([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})(Z|([+-])([0-9]{2}):([0-9]{2}))$"
NAMES="['y','mo','d','h','mi','s','z','sg','oh','om']"

con.execute("""CREATE MACRO unq(f) AS
  CASE WHEN length(f)>=2 AND left(f,1)='"' AND right(f,1)='"' THEN substr(f,2,length(f)-2) ELSE f END""")
con.execute("CREATE MACRO gi(p,k) AS TRY_CAST(struct_extract(p,k) AS INTEGER)")
con.execute("""CREATE MACRO f12(p) AS CASE
  WHEN struct_extract(p,'y')='' THEN NULL
  WHEN gi(p,'y')>=1 AND gi(p,'mo') BETWEEN 1 AND 12 AND gi(p,'d')>=1
   AND gi(p,'d')<=(CASE gi(p,'mo')
        WHEN 2 THEN (CASE WHEN (gi(p,'y')%4=0 AND gi(p,'y')%100<>0) OR gi(p,'y')%400=0 THEN 29 ELSE 28 END)
        WHEN 4 THEN 30 WHEN 6 THEN 30 WHEN 9 THEN 30 WHEN 11 THEN 30 ELSE 31 END)
   AND gi(p,'h')<24 AND gi(p,'mi')<60 AND gi(p,'s')<60
   AND (struct_extract(p,'z')='Z' OR (gi(p,'oh')<24 AND gi(p,'om')<60))
  THEN (make_date(gi(p,'y'),gi(p,'mo'),gi(p,'d'))-DATE '1970-01-01')::BIGINT*86400000
       +gi(p,'h')::BIGINT*3600000+gi(p,'mi')::BIGINT*60000+gi(p,'s')::BIGINT*1000
       -(CASE WHEN struct_extract(p,'z')='Z' THEN 0
              WHEN struct_extract(p,'sg')='-' THEN -1*(gi(p,'oh')::BIGINT*3600000+gi(p,'om')::BIGINT*60000)
              ELSE gi(p,'oh')::BIGINT*3600000+gi(p,'om')::BIGINT*60000 END)
  END""")
con.execute("""CREATE MACRO ts_ms(s,p) AS CASE
  WHEN s IS NULL THEN NULL
  WHEN regexp_full_match(s,'[0-9]+') THEN TRY_CAST(s AS BIGINT)
  ELSE f12(p) END""")

def add_ms(sub,col,out):
    return (f"SELECT * EXCLUDE(_p),ts_ms({col},_p) AS {out} FROM "
            f"(SELECT *,regexp_extract({col},'{PAT}',{NAMES}) AS _p FROM ({sub}))")

def raw(folder,ncols):
    glob=os.path.join(DATA,folder,"*.csv")
    return f"""SELECT q FROM (
      SELECT list_transform(string_split(line,','),x->unq(x)) AS q FROM (
        SELECT unnest(list_slice(l,2,len(l))) AS line FROM (
          SELECT string_split(content,chr(10)) AS l FROM read_text('{glob}')
        )
      )
    ) WHERE len(q)={ncols} AND len(list_filter(q,x->contains(x,'"')))=0"""

lap("setup")

# ---------- small clean files ----------
con.execute(f"""CREATE TEMP TABLE products AS
  SELECT product_id,category FROM read_csv('{os.path.join(DATA,'products.csv')}',header=true,all_varchar=true)""")
con.execute(f"""CREATE TEMP TABLE campaigns AS
  SELECT campaign_id,channel FROM read_csv('{os.path.join(DATA,'campaigns.csv')}',header=true,all_varchar=true)""")
lap("products+campaigns")

# ---------- links -> person map ----------
con.execute(f"""CREATE TEMP TABLE lk AS
  SELECT DISTINCT NULLIF(q[1],'') AS a,NULLIF(q[2],'') AS b FROM ({raw('links',2)})
  WHERE NULLIF(q[1],'') IS NOT NULL AND NULLIF(q[2],'') IS NOT NULL""")
lap("links parse")
# dense integer ids in byte-wise order, so the smallest index in a component is its smallest name
con.execute("""CREATE TEMP TABLE lid AS
  SELECT id,(row_number() OVER (ORDER BY id))-1 AS idx
  FROM (SELECT a AS id FROM lk WHERE a<>b UNION SELECT b FROM lk WHERE a<>b)""")
N=con.execute("SELECT count(*) FROM lid").fetchone()[0]
edges=con.execute("""SELECT x.idx,y.idx FROM lk
  JOIN lid x ON lk.a=x.id JOIN lid y ON lk.b=y.id WHERE lk.a<>lk.b""").fetchall()
lap("links intern")

parent=list(range(N))
for a,b in edges:
    while parent[a]!=a:
        parent[a]=parent[parent[a]]
        a=parent[a]
    while parent[b]!=b:
        parent[b]=parent[parent[b]]
        b=parent[b]
    if a!=b:
        if a<b: parent[b]=a
        else: parent[a]=b

ch=[]; rt=[]
for i in range(N):
    r=i
    while parent[r]!=r:
        parent[r]=parent[parent[r]]
        r=parent[r]
    if r!=i:
        ch.append(i); rt.append(r)
con.execute("""CREATE TEMP TABLE idmap AS
  SELECT x.id AS uid,y.id AS person
  FROM (SELECT unnest(?::INTEGER[]) AS idx,unnest(?::INTEGER[]) AS root) m
  JOIN lid x ON x.idx=m.idx JOIN lid y ON y.idx=m.root""",[ch,rt])
lap("union-find+idmap")

# ---------- events ----------
con.execute(f"""CREATE TEMP TABLE ev_raw AS
  SELECT NULLIF(q[1],'') AS event_id,NULLIF(q[2],'') AS user_id,NULLIF(q[3],'') AS ts,
         NULLIF(q[4],'') AS ingest_ts,NULLIF(q[5],'') AS event_type,
         NULLIF(q[6],'') AS product_id,NULLIF(q[7],'') AS price
  FROM ({raw('events',7)}) WHERE NULLIF(q[1],'') IS NOT NULL""")
lap("ev_raw (read+split)")

con.execute(f"""CREATE TEMP TABLE ev1 AS
  SELECT event_id,
         arg_min(struct_pack(user_id:=user_id,tss:=ts,event_type:=event_type,
                             product_id:=product_id,price:=price,ing:=ing),ing) AS r
  FROM ({add_ms('SELECT * FROM ev_raw','ingest_ts','ing')}) GROUP BY event_id""")
lap("ev1 (ingest_ts parse+dedup)")

con.execute(f"""CREATE TEMP TABLE ev2 AS
  SELECT user_id,event_id,tms AS ts,event_type,product_id,
         CASE WHEN event_type='purchase' THEN TRY_CAST(price AS BIGINT) END AS price
  FROM ({add_ms('''SELECT event_id,r.user_id AS user_id,r.tss AS tss,r.event_type AS event_type,
                   r.product_id AS product_id,r.price AS price,r.ing AS ing FROM ev1''','tss','tms')})
  WHERE user_id IS NOT NULL AND tms>={WS} AND tms<{WE}
    AND event_type IN ('view','cart','purchase') AND product_id IS NOT NULL
    AND (event_type<>'purchase' OR (regexp_full_match(price,'[0-9]+')
         AND TRY_CAST(price AS BIGINT) BETWEEN 1 AND 10000000))
    AND ing-tms<={DAY}""")
lap("ev2 (ts parse+validate)")

con.execute("""CREATE TEMP TABLE ev3 AS
  SELECT COALESCE(m.person,e.user_id) AS person,e.ts,e.event_id,e.price,
         e.event_type='purchase' AS is_p,COALESCE(p.category,'unknown') AS category
  FROM ev2 e LEFT JOIN idmap m ON e.user_id=m.uid
             LEFT JOIN products p ON e.product_id=p.product_id""")
lap("ev3 (person+category joins)")

con.execute("""CREATE TEMP TABLE ev4 AS
  SELECT *,sum(newf) OVER (PARTITION BY person ORDER BY ts,event_id) AS sid FROM (
    SELECT *,CASE WHEN lag(ts) OVER w IS NULL OR ts-lag(ts) OVER w>1800000 THEN 1 ELSE 0 END AS newf
    FROM ev3 WINDOW w AS (PARTITION BY person ORDER BY ts,event_id))""")
lap("ev4 (sessionize windows)")

con.execute("""CREATE TEMP TABLE sess AS
  SELECT person,sid,min(ts) AS sstart,epoch_ms(min(ts))::DATE AS day,count(*) AS events,
         COALESCE(sum(price),0)::BIGINT AS rev,bool_or(is_p) AS has_p
  FROM ev4 GROUP BY person,sid""")
lap("sess")

con.execute("""CREATE TEMP TABLE top AS
  SELECT person,day,category FROM (
    SELECT e.person,s.day,e.category,sum(e.price) AS tot FROM ev4 e
    JOIN sess s ON e.person=s.person AND e.sid=s.sid WHERE e.is_p GROUP BY e.person,s.day,e.category)
  QUALIFY row_number() OVER (PARTITION BY person,day ORDER BY tot DESC,category ASC)=1""")
lap("top category")

# ---------- clicks ----------
con.execute(f"""CREATE TEMP TABLE ck0 AS
  SELECT DISTINCT ON (click_id) click_id,user_id,campaign_id,tss FROM (
    SELECT NULLIF(q[1],'') AS click_id,NULLIF(q[2],'') AS user_id,
           NULLIF(q[3],'') AS campaign_id,NULLIF(q[4],'') AS tss
    FROM ({raw('clicks',4)})) WHERE click_id IS NOT NULL""")
lap("ck0 (read+dedup)")

con.execute(f"""CREATE TEMP TABLE ck AS
  SELECT COALESCE(m.person,c.user_id) AS person,c.click_id,c.tms AS ts,c.campaign_id
  FROM ({add_ms('SELECT * FROM ck0','tss','tms')}) c
  JOIN campaigns ca ON c.campaign_id=ca.campaign_id
  LEFT JOIN idmap m ON c.user_id=m.uid
  WHERE c.user_id IS NOT NULL AND c.tms>={WS-WEEK} AND c.tms<{WE}""")
lap("ck (validate+person)")

con.execute("""CREATE TEMP TABLE ck1 AS
  SELECT person,ts,arg_min(campaign_id,click_id) AS campaign_id FROM ck GROUP BY person,ts""")
con.execute(f"""CREATE TEMP TABLE cred AS
  SELECT s.day,s.has_p,s.rev,
         CASE WHEN c.ts IS NOT NULL AND s.sstart-c.ts<={WEEK} THEN c.campaign_id ELSE 'organic' END AS campaign_id
  FROM sess s ASOF LEFT JOIN ck1 c ON s.person=c.person AND s.sstart>=c.ts""")
lap("cred (asof credit)")

con.execute(f"""CREATE TEMP TABLE asst AS
  SELECT campaign_id,day,count(*)::BIGINT AS assisted FROM (
    SELECT DISTINCT s.person,s.sstart,s.day,c.campaign_id FROM sess s
    JOIN ck c ON c.person=s.person AND c.ts<=s.sstart AND c.ts>=s.sstart-{WEEK})
  GROUP BY campaign_id,day""")
lap("asst (assists join)")

# ---------- outputs ----------
os.makedirs(os.path.join(OUT,"user_daily"),exist_ok=True)
os.makedirs(os.path.join(OUT,"campaign_daily"),exist_ok=True)

con.execute(f"""COPY (
  SELECT s.person AS user_id,s.day AS day,count(*)::BIGINT AS sessions,sum(s.events)::BIGINT AS events,
         sum(s.rev)::BIGINT AS revenue_cents,t.category AS top_category
  FROM sess s LEFT JOIN top t ON s.person=t.person AND s.day=t.day
  GROUP BY s.person,s.day,t.category
) TO '{os.path.join(OUT,'user_daily','part-0.parquet')}' (FORMAT parquet)""")
lap("write user_daily")

con.execute(f"""COPY (
  SELECT k.campaign_id,CASE WHEN k.campaign_id='organic' THEN 'organic' ELSE ca.channel END AS channel,
         k.day,k.sessions,k.purchasing_sessions,k.revenue_cents,k.assisted_sessions
  FROM (
    SELECT COALESCE(a.campaign_id,b.campaign_id) AS campaign_id,COALESCE(a.day,b.day) AS day,
           COALESCE(a.sessions,0)::BIGINT AS sessions,
           COALESCE(a.purchasing_sessions,0)::BIGINT AS purchasing_sessions,
           COALESCE(a.revenue_cents,0)::BIGINT AS revenue_cents,
           COALESCE(b.assisted,0)::BIGINT AS assisted_sessions
    FROM (SELECT campaign_id,day,count(*) AS sessions,count(*) FILTER (WHERE has_p) AS purchasing_sessions,
                 sum(rev) AS revenue_cents FROM cred GROUP BY campaign_id,day) a
    FULL JOIN asst b ON a.campaign_id=b.campaign_id AND a.day=b.day
  ) k LEFT JOIN campaigns ca ON ca.campaign_id=k.campaign_id
) TO '{os.path.join(OUT,'campaign_daily','part-0.parquet')}' (FORMAT parquet)""")
lap("write campaign_daily")
print("done")