#!/usr/bin/env python3
import os
import subprocess
import duckdb

HERE=os.path.dirname(os.path.abspath(__file__))
OUT=os.environ.get("OUTPUT_DIR","/output")
SCR=os.environ.get("SCRATCH_DIR","/tmp")
os.environ.setdefault("SCRATCH_DIR",SCR)

subprocess.run([os.path.join(HERE,"sessions")],check=True)

os.makedirs(os.path.join(OUT,"user_daily"),exist_ok=True)
os.makedirs(os.path.join(OUT,"campaign_daily"),exist_ok=True)
con=duckdb.connect()
opts=f"delim='{chr(1)}',quote='',escape='',header=false,auto_detect=false,strict_mode=false"
con.execute(f"""COPY (
  SELECT replace(u,chr(2),chr(13)) AS user_id,d::DATE AS day,s::BIGINT AS sessions,
         e::BIGINT AS events,r::BIGINT AS revenue_cents,NULLIF(t,'') AS top_category
  FROM read_csv('{SCR}/ud.txt',columns={{'u':'VARCHAR','d':'VARCHAR','s':'VARCHAR','e':'VARCHAR','r':'VARCHAR','t':'VARCHAR'}},{opts})
) TO '{OUT}/user_daily/part-0.parquet' (FORMAT parquet)""")
con.execute(f"""COPY (
  SELECT c AS campaign_id,h AS channel,d::DATE AS day,s::BIGINT AS sessions,
         p::BIGINT AS purchasing_sessions,r::BIGINT AS revenue_cents,a::BIGINT AS assisted_sessions
  FROM read_csv('{SCR}/cd.txt',columns={{'c':'VARCHAR','h':'VARCHAR','d':'VARCHAR','s':'VARCHAR','p':'VARCHAR','r':'VARCHAR','a':'VARCHAR'}},{opts})
) TO '{OUT}/campaign_daily/part-0.parquet' (FORMAT parquet)""")