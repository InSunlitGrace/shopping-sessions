#include <bits/stdc++.h>
#include <immintrin.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
using namespace std;
typedef string_view sv;
typedef long long ll;
typedef unsigned __int128 u128;

#ifndef MADV_POPULATE_WRITE
#define MADV_POPULATE_WRITE 23
#endif

static const ll BAD=LLONG_MIN;
static const ll DAY=86400000LL;
static const ll WEEK=7*DAY;
static const ll GAP=1800000LL;
static const int NS=1024;
static const int NB=256;
static int NT=8;
static ll D0;
static int ND;
static atomic<bool> gPop(true);

static double nowt(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static double TL=nowt();
static void lap(const char*n){double t=nowt();fprintf(stderr,"%s: %.2fs\n",n,t-TL);TL=t;}

template<class F> static void pfor(size_t n,F f){
    atomic<size_t> nx(0);
    auto w=[&](int tid){
        for(;;){
            size_t i=nx.fetch_add(1);
            if(i>=n)break;
            f(i,tid);
        }
    };
    vector<thread> th;
    for(int t=1;t<NT;t++)th.emplace_back(w,t);
    w(0);
    for(auto&x:th)x.join();
}

// All callers pass views into mmapped input (which has a zero slack page after it),
// so reading 16 bytes from the start of a short string is safe.
static inline uint64_t hsh(sv s){
    size_t n=s.size();
    const char*p=s.data();
    if(n<=16){
        uint64_t a,b;
        memcpy(&a,p,8);
        memcpy(&b,p+8,8);
        uint64_t ma=n>=8?~0ULL:((1ULL<<(8*(n&7)))-1);
        uint64_t mb=n>=16?~0ULL:(n>8?((1ULL<<(8*(n-8)))-1):0ULL);
        a&=ma;b&=mb;
        uint64_t h=(a^((uint64_t)n*0x9E3779B97F4A7C15ULL))*0xff51afd7ed558ccdULL;
        h^=h>>32;
        h=(h^b)*0xc4ceb9fe1a85ec53ULL;
        h^=h>>29;
        h*=0x9E3779B97F4A7C15ULL;
        h^=h>>32;
        return h;
    }
    uint64_t h=0x9E3779B97F4A7C15ULL^(uint64_t)n;
    while(n>=8){
        uint64_t v;
        memcpy(&v,p,8);
        h=(h^v)*0xff51afd7ed558ccdULL;
        h^=h>>32;
        p+=8;n-=8;
    }
    if(n){
        uint64_t v=0;
        memcpy(&v,p,n);
        h=(h^v)*0xc4ceb9fe1a85ec53ULL;
        h^=h>>29;
    }
    h*=0x9E3779B97F4A7C15ULL;
    h^=h>>32;
    return h;
}
static inline int shardH(uint64_t h,int n){return (int)((h>>40)%(uint64_t)n);}

// flat open-addressing map from string to dense index (hash+index interleaved)
struct FM{
    struct Sl{uint64_t h;uint32_t i;};
    vector<Sl> t;
    vector<sv> keys;
    size_t mask=0;
    bool collide=false; // two different keys share a 64-bit hash
    FM(){init(8);}
    void init(size_t want){
        size_t c=16;
        while(c<want*2)c<<=1;
        t.assign(c,Sl{0,0});
        mask=c-1;
    }
    uint32_t find(sv k,uint64_t h)const{
        size_t i=h&mask;
        while(t[i].i){
            if(t[i].h==h&&keys[t[i].i-1]==k)return t[i].i-1;
            i=(i+1)&mask;
        }
        return UINT32_MAX;
    }
    // hash-only lookup, valid only when !collide
    uint32_t findh(uint64_t h)const{
        size_t i=h&mask;
        while(t[i].i){
            if(t[i].h==h)return t[i].i-1;
            i=(i+1)&mask;
        }
        return UINT32_MAX;
    }
    uint32_t add(sv k,uint64_t h,bool&added){
        if((keys.size()+1)*2>t.size())grow();
        size_t i=h&mask;
        while(t[i].i){
            if(t[i].h==h){
                if(keys[t[i].i-1]==k){added=false;return t[i].i-1;}
                collide=true;
            }
            i=(i+1)&mask;
        }
        keys.push_back(k);
        t[i]=Sl{h,(uint32_t)keys.size()};
        added=true;
        return (uint32_t)keys.size()-1;
    }
    void grow(){
        vector<Sl> o;
        o.swap(t);
        size_t c=o.size()*2;
        t.assign(c,Sl{0,0});
        mask=c-1;
        for(size_t j=0;j<o.size();j++)if(o[j].i){
            size_t i=o[j].h&mask;
            while(t[i].i)i=(i+1)&mask;
            t[i]=o[j];
        }
    }
};

// per-bucket person map keyed by the 64-bit hash only (people never collide in practice)
struct HM{
    struct S{uint64_t h;uint32_t i;};
    vector<S> t;
    vector<sv> names;
    size_t mask;
    HM(){t.assign(1024,S{0,0});mask=1023;}
    uint32_t add(uint64_t h,sv name){
        if((names.size()+1)*2>t.size())grow();
        size_t i=h&mask;
        while(t[i].i){
            if(t[i].h==h)return t[i].i-1;
            i=(i+1)&mask;
        }
        names.push_back(name);
        t[i]=S{h,(uint32_t)names.size()};
        return (uint32_t)names.size()-1;
    }
    void grow(){
        vector<S> o;
        o.swap(t);
        size_t c=o.size()*2;
        t.assign(c,S{0,0});
        mask=c-1;
        for(auto&s:o)if(s.i){
            size_t i=s.h&mask;
            while(t[i].i)i=(i+1)&mask;
            t[i]=s;
        }
    }
};

static vector<string> listcsv(const string&dir){
    vector<string> r;
    DIR*d=opendir(dir.c_str());
    if(!d)return r;
    while(dirent*e=readdir(d)){
        string n=e->d_name;
        if(n.size()>4&&n.substr(n.size()-4)==".csv")r.push_back(dir+"/"+n);
    }
    closedir(d);
    sort(r.begin(),r.end());
    return r;
}

struct Chunk{const char*b;const char*e;};

// Maps each file followed by anonymous zero pages, so reads a little past the end are safe.
static void addChunks(const vector<string>&paths,vector<Chunk>&out,size_t CH){
    for(const string&path:paths){
        int fd=open(path.c_str(),O_RDONLY);
        if(fd<0)continue;
        struct stat st;
        fstat(fd,&st);
        if(st.st_size==0){close(fd);continue;}
        size_t len=(size_t)st.st_size;
        size_t maplen=(len+4095)&~(size_t)4095;
        char*base=(char*)mmap(0,maplen+8192,PROT_READ,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(base==(char*)MAP_FAILED){close(fd);continue;}
        void*r=mmap(base,len,PROT_READ,MAP_PRIVATE|MAP_FIXED,fd,0);
        close(fd);
        if(r==MAP_FAILED){munmap(base,maplen+8192);continue;}
        const char*p=base;
        const char*e=p+len;
        const char*nl=(const char*)memchr(p,'\n',e-p);
        if(!nl)continue;
        const char*s=nl+1;
        while(s<e){
            const char*t=s+CH;
            if(t>=e){out.push_back({s,e});break;}
            const char*q=(const char*)memchr(t,'\n',e-t);
            const char*en=q?q+1:e;
            out.push_back({s,en});
            s=en;
        }
    }
}

// rough row-count estimate from the first couple of chunks (only used to pre-size buffers)
static size_t estRows(const vector<Chunk>&cs){
    size_t tb=0;
    for(auto&c:cs)tb+=(size_t)(c.e-c.b);
    size_t sb=0,sl=0;
    for(size_t k=0;k<cs.size()&&k<2;k++){
        const char*p=cs[k].b,*e=cs[k].e;
        sb+=(size_t)(e-p);
        while(p<e){
            const char*q=(const char*)memchr(p,'\n',e-p);
            if(!q)break;
            sl++;
            p=q+1;
        }
    }
    return sb?(size_t)((double)tb/(double)sb*(double)sl):0;
}

// ---------------- line parsing ----------------

// bytes equal to pat get their high bit set (lowest set bit is exact)
static inline uint64_t hasb(uint64_t x,uint64_t pat){
    uint64_t v=x^pat;
    return (v-0x0101010101010101ULL)&~v&0x8080808080808080ULL;
}
// first of ',', '"', '\n' at or after p (or e)
static inline const char* scan3(const char*p,const char*e){
    while(p+8<=e){
        uint64_t x;
        memcpy(&x,p,8);
        uint64_t m=hasb(x,0x2C2C2C2C2C2C2C2CULL)|hasb(x,0x2222222222222222ULL)|hasb(x,0x0A0A0A0A0A0A0A0AULL);
        if(m)return p+(__builtin_ctzll(m)>>3);
        p+=8;
    }
    while(p<e&&*p!=','&&*p!='\n'&&*p!='"')p++;
    return p;
}

// Generic single-pass line parser. Semantics: split on commas, strip one pair of
// surrounding quotes, reject any other quote, require exactly nc fields.
// Always sets next to the start of the following line.
static inline bool parseLine(const char*p,const char*e,int nc,sv*out,const char*&next){
    int k=0;
    for(;;){
        if(k>=nc)goto bad;
        if(p<e&&*p=='"'){
            p++;
            const char*s=p;
            p=scan3(p,e);
            if(p>=e||*p!='"')goto bad;
            out[k++]=sv(s,(size_t)(p-s));
            p++;
            if(p<e&&*p!=','&&*p!='\n')goto bad;
        }else{
            const char*s=p;
            p=scan3(p,e);
            if(p<e&&*p=='"')goto bad;
            out[k++]=sv(s,(size_t)(p-s));
        }
        if(p>=e){next=e;return k==nc;}
        if(*p=='\n'){next=p+1;return k==nc;}
        p++; // skip comma
    }
bad:
    {
        const char*q=p<e?(const char*)memchr(p,'\n',(size_t)(e-p)):nullptr;
        next=q?q+1:e;
        return false;
    }
}

static inline int ctz128(u128 x){
    uint64_t lo=(uint64_t)x;
    return lo?__builtin_ctzll(lo):64+__builtin_ctzll((uint64_t)(x>>64));
}
static inline int pop128(u128 x){
    return __builtin_popcountll((uint64_t)x)+__builtin_popcountll((uint64_t)(x>>64));
}
// masks of ',' '"' '\n' over the 128 bytes starting at p (bit i = byte i)
static inline void scan128(const char*p,u128&cm,u128&qm,u128&nm){
    const __m256i vc=_mm256_set1_epi8(',');
    const __m256i vq=_mm256_set1_epi8('"');
    const __m256i vn=_mm256_set1_epi8('\n');
    uint64_t c[2],q[2],n[2];
    for(int h=0;h<2;h++){
        __m256i a=_mm256_loadu_si256((const __m256i*)(p+64*h));
        __m256i b=_mm256_loadu_si256((const __m256i*)(p+64*h+32));
        c[h]=(uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(a,vc))
            |((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(b,vc))<<32);
        q[h]=(uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(a,vq))
            |((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(b,vq))<<32);
        n[h]=(uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(a,vn))
            |((uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(b,vn))<<32);
    }
    cm=((u128)c[1]<<64)|c[0];
    qm=((u128)q[1]<<64)|q[0];
    nm=((u128)n[1]<<64)|n[0];
}

// Fast path: handles lines of up to 127 bytes. Returns false (without side effects that
// matter) for anything unusual; the caller then uses the generic parser, so semantics
// are identical either way.
static inline bool fastLine(const char*p,int nc,sv*out,const char*&next){
    u128 cm,qm,nm;
    scan128(p,cm,qm,nm);
    if(!nm)return false;
    int L=ctz128(nm);
    u128 lim=(((u128)1)<<L)-1;
    cm&=lim;
    qm&=lim;
    if(pop128(cm)!=nc-1)return false;
    int s[8],en[8];
    s[0]=0;
    u128 m=cm;
    for(int k=0;k<nc-1;k++){
        int c=ctz128(m);
        m&=m-1;
        en[k]=c;
        s[k+1]=c+1;
    }
    en[nc-1]=L;
    int nq=0;
    bool ok=true;
    for(int k=0;k<nc;k++){
        int a=s[k],b=en[k];
        if(b>a&&(int)((qm>>a)&1)){
            ok=ok&&(b-a>=2)&&(int)((qm>>(b-1))&1);
            nq+=2;
            out[k]=sv(p+a+1,(size_t)(b-a>=2?b-a-2:0));
        }else{
            out[k]=sv(p+a,(size_t)(b-a));
        }
    }
    if(!ok||pop128(qm)!=nq)return false;
    next=p+L+1;
    return true;
}

template<class F> static void rows(const Chunk&ch,int nc,F f){
    const char*p=ch.b;
    const char*e=ch.e;
    sv fields[8];
    while(p<e){
        const char*next;
        bool ok=fastLine(p,nc,fields,next);
        if(!ok)ok=parseLine(p,e,nc,fields,next);
        if(ok)f(fields);
        p=next;
    }
}

template<class F> static void eachrow(const vector<string>&paths,int nc,F f){
    vector<Chunk> c;
    addChunks(paths,c,(size_t)1<<40);
    for(auto&ch:c)rows(ch,nc,f);
}

static inline bool dg(char c){return c>='0'&&c<='9';}
static inline int num(const char*p,int n){
    int v=0;
    for(int k=0;k<n;k++)v=v*10+(p[k]-'0');
    return v;
}
static ll days_from_civil(ll y,int m,int d){
    y-=m<=2;
    ll era=(y>=0?y:y-399)/400;
    ll yoe=y-era*400;
    ll doy=(153*(m>2?m-3:m+9)+2)/5+d-1;
    ll doe=yoe*365+yoe/4-yoe/100+doy;
    return era*146097+doe-719468;
}

struct DC{uint64_t k0;uint16_t k1;bool v;ll days;};

// n is 20 or 25 and p[4]=='-' (so it cannot be an all-digit string)
static ll parse_iso(const char*p,size_t n){
    if(p[4]!='-'||p[7]!='-'||p[10]!='T'||p[13]!=':'||p[16]!=':')return BAD;
    int pos[]={0,1,2,3,5,6,8,9,11,12,14,15,17,18};
    for(int k=0;k<14;k++)if(!dg(p[pos[k]]))return BAD;
    ll off=0;
    if(n==20){
        if(p[19]!='Z')return BAD;
    }else{
        if(p[19]!='+'&&p[19]!='-')return BAD;
        if(p[22]!=':')return BAD;
        if(!dg(p[20])||!dg(p[21])||!dg(p[23])||!dg(p[24]))return BAD;
        int oh=num(p+20,2);
        int om=num(p+23,2);
        if(oh>23||om>59)return BAD;
        off=(oh*3600000LL+om*60000LL)*(p[19]=='-'?-1:1);
    }
    int h=num(p+11,2),mi=num(p+14,2),se=num(p+17,2);
    if(h>23||mi>59||se>59)return BAD;
    // date part, cached per thread (only valid dates are cached)
    static thread_local DC dcache[32];
    uint64_t k0;
    uint16_t k1;
    memcpy(&k0,p,8);
    memcpy(&k1,p+8,2);
    DC&c=dcache[((p[8]-'0')*10+(p[9]-'0'))&31];
    ll days;
    if(c.v&&c.k0==k0&&c.k1==k1){
        days=c.days;
    }else{
        int y=num(p,4),mo=num(p+5,2),d=num(p+8,2);
        if(mo<1||mo>12||d<1)return BAD;
        static const int dim[]={31,28,31,30,31,30,31,31,30,31,30,31};
        int mx=dim[mo-1];
        if(mo==2&&((y%4==0&&y%100!=0)||y%400==0))mx=29;
        if(d>mx)return BAD;
        days=days_from_civil(y,mo,d);
        c.k0=k0;c.k1=k1;c.v=true;c.days=days;
    }
    return ((days*86400LL+h*3600LL+mi*60LL+se)*1000LL)-off;
}

static ll parse_ts(sv s){
    size_t n=s.size();
    const char*p=s.data();
    if((n==20||n==25)&&p[4]=='-')return parse_iso(p,n);
    if(n==0)return BAD;
    for(size_t k=0;k<n;k++)if(!dg(p[k]))return BAD;
    size_t k=0;
    while(k<n-1&&p[k]=='0')k++;
    if(n-k>18)return BAD;
    ll v=0;
    for(;k<n;k++)v=v*10+(p[k]-'0');
    return v;
}

static ll parse_price(sv s){
    size_t n=s.size();
    if(n==0)return -1;
    for(size_t k=0;k<n;k++)if(!dg(s[k]))return -1;
    size_t k=0;
    while(k<n-1&&s[k]=='0')k++;
    if(n-k>8)return -1;
    ll v=0;
    for(;k<n;k++)v=v*10+(s[k]-'0');
    return (v>=1&&v<=10000000)?v:-1;
}

// ---------------- minimal Parquet writer ----------------
struct TW{
    string b;vector<int> st;int cur=0;
    void varint(uint64_t v){while(v>=0x80){b.push_back((char)(v|0x80));v>>=7;}b.push_back((char)v);}
    void zz(ll v){varint(((uint64_t)v<<1)^(uint64_t)(v>>63));}
    void fld(int id,int type){
        int d=id-cur;
        if(d>0&&d<=15)b.push_back((char)((d<<4)|type));
        else{b.push_back((char)type);zz(id);}
        cur=id;
    }
    void i32(int id,int v){fld(id,5);zz(v);}
    void i64(int id,ll v){fld(id,6);zz(v);}
    void str(int id,const string&s){fld(id,8);varint(s.size());b+=s;}
    void lst(int id,int et,size_t n){
        fld(id,9);
        if(n<15)b.push_back((char)((n<<4)|et));
        else{b.push_back((char)(0xF0|et));varint(n);}
    }
    void sbeg(int id){fld(id,12);st.push_back(cur);cur=0;}
    void lbeg(){st.push_back(cur);cur=0;}
    void send(){b.push_back(0);cur=st.back();st.pop_back();}
};
struct Col{
    string name;int type;int conv;bool opt;string data;
    vector<uint8_t> defd;
    void a32(int32_t v){data.append((const char*)&v,4);}
    void a64(int64_t v){data.append((const char*)&v,8);}
    void as(sv s){uint32_t n=(uint32_t)s.size();data.append((const char*)&n,4);data.append(s.data(),s.size());}
};
static void writeParquet(const string&path,vector<Col>&cols,ll nrows){
    string f="PAR1";
    vector<ll> offs,sizes;
    for(auto&c:cols){
        string body;
        if(c.opt){
            size_t n=c.defd.size(),ng=(n+7)/8;
            TW t;
            t.varint((ng<<1)|1);
            string lv=t.b;
            string bits(ng,'\0');
            for(size_t i=0;i<n;i++)if(c.defd[i])bits[i/8]|=(char)(1<<(i%8));
            lv+=bits;
            uint32_t ln=(uint32_t)lv.size();
            body.append((const char*)&ln,4);
            body+=lv;
        }
        body+=c.data;
        TW h;
        h.i32(1,0);
        h.i32(2,(int)body.size());
        h.i32(3,(int)body.size());
        h.sbeg(5);
        h.i32(1,(int)nrows);
        h.i32(2,0);
        h.i32(3,3);
        h.i32(4,3);
        h.send();
        h.b.push_back(0);
        offs.push_back((ll)f.size());
        sizes.push_back((ll)(h.b.size()+body.size()));
        f+=h.b;
        f+=body;
    }
    TW m;
    m.i32(1,1);
    m.lst(2,12,cols.size()+1);
    m.lbeg();m.str(4,"schema");m.i32(5,(int)cols.size());m.send();
    for(auto&c:cols){
        m.lbeg();
        m.i32(1,c.type);
        m.i32(3,c.opt?1:0);
        m.str(4,c.name);
        if(c.conv>=0)m.i32(6,c.conv);
        m.send();
    }
    m.i64(3,nrows);
    m.lst(4,12,1);
    m.lbeg();
    m.lst(1,12,cols.size());
    ll total=0;
    for(size_t k=0;k<cols.size();k++){
        m.lbeg();
        m.i64(2,offs[k]);
        m.sbeg(3);
        m.i32(1,cols[k].type);
        m.lst(2,5,2);m.zz(0);m.zz(3);
        m.lst(3,8,1);m.varint(cols[k].name.size());m.b+=cols[k].name;
        m.i32(4,0);
        m.i64(5,nrows);
        m.i64(6,sizes[k]);
        m.i64(7,sizes[k]);
        m.i64(9,offs[k]);
        m.send();
        m.send();
        total+=sizes[k];
    }
    m.i64(2,total);
    m.i64(3,nrows);
    m.send();
    m.b.push_back(0);
    f+=m.b;
    uint32_t fl=(uint32_t)m.b.size();
    f.append((const char*)&fl,4);
    f+="PAR1";
    FILE*fp=fopen(path.c_str(),"wb");
    if(!fp){fprintf(stderr,"cannot write %s\n",path.c_str());exit(1);}
    fwrite(f.data(),1,f.size(),fp);
    fclose(fp);
}

// ---- links (built on its own thread, read-only afterwards) ----
static FM lm;
static vector<uint32_t> par;
struct Root{sv name;uint64_t h;};
static vector<Root> root;
static uint32_t lget(sv s){
    bool a;
    return lm.add(s,hsh(s),a);
}
static uint32_t findr(uint32_t x){
    while(par[x]!=x){
        par[x]=par[par[x]];
        x=par[x];
    }
    return x;
}
static inline uint32_t linkFind(sv name,uint64_t nh){
    return lm.collide?lm.find(name,nh):lm.findh(nh);
}

// 64 bytes: one cache line
struct EvRec{
    const char*idp;const char*up;
    uint64_t h,uh;
    ll ts,ing;
    int32_t price;
    uint32_t idl,ul;
    uint8_t cat,type,pe;
};
struct EvB{sv name;uint64_t nh;ll ts;int32_t price;uint8_t cat;};
struct CkB{sv name;uint64_t nh;ll ts;uint32_t camp;sv id;};
struct E3{ll ts;int32_t price;uint8_t cat;};
struct K3{ll ts;uint32_t camp;sv id;};
struct CA{ll s=0,ps=0,rev=0,as=0;};
struct UdRow{sv name;int32_t day;ll s,e,r;int cat;};
struct Out{vector<UdRow> ud;};
struct Scr{
    vector<uint32_t> pe,pc,es,cs,ep,cp;
    vector<E3> ev;
    vector<K3> ck;
};

// fault the pages of a freshly reserved buffer in bulk (cheaper than one fault per page)
static inline void prefault(void*p,size_t bytes){
    if(!gPop.load(std::memory_order_relaxed))return;
    uintptr_t a=((uintptr_t)p+4095)&~(uintptr_t)4095;
    uintptr_t b=((uintptr_t)p+bytes)&~(uintptr_t)4095;
    if(b>a&&madvise((void*)a,b-a,MADV_POPULATE_WRITE)!=0)gPop.store(false,std::memory_order_relaxed);
}

template<class V,class T> static inline void pushR(V&v,const T&x,size_t per){
    if(v.capacity()==0){
        v.reserve(per);
        prefault((void*)v.data(),v.capacity()*sizeof(T));
    }
    v.push_back(x);
}

template<class T,class C> static inline void sortRange(T*a,size_t m,C cmp){
    if(m<2)return;
    if(m<=24){
        for(size_t i=1;i<m;i++){
            T v=a[i];
            size_t j=i;
            while(j>0&&cmp(v,a[j-1])){a[j]=a[j-1];j--;}
            a[j]=v;
        }
    }else sort(a,a+m,cmp);
}

int main(){
    const char*e1=getenv("DATA_DIR");
    const char*e3=getenv("NCPU");
    string DATA=e1?e1:"/data";
    string OUT=getenv("OUTPUT_DIR")?getenv("OUTPUT_DIR"):"/output";
    if(e3)NT=max(1,atoi(e3));
    else NT=max(1,(int)thread::hardware_concurrency());

    ll WS,WE;
    {
        ifstream in(DATA+"/manifest.json");
        string m((istreambuf_iterator<char>(in)),istreambuf_iterator<char>());
        auto getw=[&](const char*key){
            size_t pos=m.find(key);
            pos=m.find(':',pos);
            size_t q1=m.find('"',pos);
            size_t q2=m.find('"',q1+1);
            return parse_ts(sv(m).substr(q1+1,q2-q1-1));
        };
        WS=getw("window_start");
        WE=getw("window_end");
    }
    if(WS==BAD||WE==BAD||WE<=WS){
        fprintf(stderr,"bad or missing manifest in %s\n",DATA.c_str());
        return 1;
    }
    D0=WS/DAY;
    ND=(int)((WE-1)/DAY-D0+1);

    set<sv> catset;
    vector<pair<sv,sv>> prows;
    eachrow({DATA+"/products.csv"},2,[&](sv*f){
        catset.insert(f[1]);
        prows.push_back({f[0],f[1]});
    });
    catset.insert(sv("unknown"));
    vector<sv> catnames(catset.begin(),catset.end());
    map<sv,uint8_t> catid;
    for(size_t k=0;k<catnames.size();k++)catid[catnames[k]]=(uint8_t)k;
    uint8_t unk=catid[sv("unknown")];
    FM pm;
    pm.init(prows.size());
    vector<uint8_t> pcat;
    for(auto&pr:prows){
        bool a;
        uint32_t i=pm.add(pr.first,hsh(pr.first),a);
        if(a)pcat.push_back(catid[pr.second]);
        else pcat[i]=catid[pr.second];
    }
    int ncat=(int)catnames.size();

    FM cm;
    vector<sv> campid,campch;
    eachrow({DATA+"/campaigns.csv"},2,[&](sv*f){
        bool a;
        uint32_t i=cm.add(f[0],hsh(f[0]),a);
        if(a){campid.push_back(f[0]);campch.push_back(f[1]);}
        else campch[i]=f[1];
    });
    uint32_t ncamp=(uint32_t)campid.size();
    uint32_t organic=ncamp;
    lap("setup");

    // ---- links build on its own thread, overlapped with phase A ----
    vector<Chunk> echunks;
    addChunks(listcsv(DATA+"/events"),echunks,4<<20);
    size_t estE=estRows(echunks);
    size_t perLoc=estE/((size_t)NT*NS)*5/4+32;
    size_t perEb=estE/((size_t)NT*NB)*5/4+32;
    vector<vector<vector<EvRec>>> loc(NT,vector<vector<EvRec>>(NS));

    size_t lbytes=0;
    for(auto&pth:listcsv(DATA+"/links")){
        struct stat st;
        if(stat(pth.c_str(),&st)==0)lbytes+=(size_t)st.st_size;
    }

    double tStart=nowt();
    double tLinks=0,tA=0;
    thread lt([&]{
        size_t wantL=lbytes/21*2+16;
        lm.init(wantL);
        lm.keys.reserve(wantL);
        vector<pair<uint32_t,uint32_t>> edges;
        edges.reserve(lbytes/21+16);
        eachrow(listcsv(DATA+"/links"),2,[&](sv*f){
            if(f[0].empty()||f[1].empty())return;
            uint32_t a=lget(f[0]);
            uint32_t b=lget(f[1]);
            edges.push_back({a,b});
        });
        par.resize(lm.keys.size());
        for(size_t k=0;k<par.size();k++)par[k]=(uint32_t)k;
        for(auto&ed:edges){
            uint32_t ra=findr(ed.first);
            uint32_t rb=findr(ed.second);
            if(ra==rb)continue;
            if(lm.keys[ra]<lm.keys[rb])par[rb]=ra;
            else par[ra]=rb;
        }
        for(size_t k=0;k<par.size();k++)par[k]=findr((uint32_t)k);
        root.resize(par.size());
        for(size_t k=0;k<par.size();k++){
            sv nm=lm.keys[par[k]];
            root[k]={nm,hsh(nm)};
        }
        tLinks=nowt()-tStart;
    });

    // ---- phase A: parallel parse events into shards ----
    pfor(echunks.size(),[&](size_t ci,int tid){
        rows(echunks[ci],7,[&](sv*f){
            if(f[0].empty())return;
            ll ing=parse_ts(f[3]);
            if(ing==BAD)ing=LLONG_MAX;
            uint8_t type=0;
            if(f[4]=="view")type=1;
            else if(f[4]=="cart")type=2;
            else if(f[4]=="purchase")type=3;
            EvRec r;
            r.idp=f[0].data();r.idl=(uint32_t)f[0].size();
            r.up=f[1].data();r.ul=(uint32_t)f[1].size();
            r.h=hsh(f[0]);
            r.uh=f[1].empty()?0:hsh(f[1]);
            r.ts=parse_ts(f[2]);
            r.ing=ing;
            r.price=type==3?(int32_t)parse_price(f[6]):-1;
            r.cat=0;
            if(type==3&&r.price>=1){
                uint32_t pi=pm.find(f[5],hsh(f[5]));
                r.cat=pi==UINT32_MAX?unk:pcat[pi];
            }
            r.type=type;
            r.pe=f[5].empty()?1:0;
            pushR(loc[tid][shardH(r.h,NS)],r,perLoc);
        });
    });
    tA=nowt()-tStart;
    lt.join();
    lap("links+A");
    fprintf(stderr,"  probe: A finished at %.2fs, links thread at %.2fs (since A start)\n",tA,tLinks);

    // ---- phase B: dedup per shard, validate, route to person buckets ----
    vector<vector<vector<EvB>>> eb(NT,vector<vector<EvB>>(NB));
    vector<vector<uint64_t>> tabs(NT);
    vector<vector<const EvRec*>> keptv(NT);
    pfor(NS,[&](size_t s,int tid){
        size_t tot=0;
        for(int t=0;t<NT;t++)tot+=loc[t][s].size();
        size_t cap=16;
        while(cap<tot*2)cap<<=1;
        size_t mask=cap-1;
        vector<uint64_t>&tab=tabs[tid];
        if(tab.size()<cap)tab.resize(cap);
        memset(tab.data(),0,cap*sizeof(uint64_t));
        vector<const EvRec*>&kept=keptv[tid];
        kept.clear();
        kept.reserve(tot);
        for(int t=0;t<NT;t++)for(const EvRec&r:loc[t][s]){
            uint64_t tagv=(r.h>>32)<<32;
            size_t i=r.h&mask;
            for(;;){
                uint64_t sl=tab[i];
                uint32_t idx=(uint32_t)sl;
                if(!idx){
                    kept.push_back(&r);
                    tab[i]=tagv|(uint32_t)kept.size();
                    break;
                }
                if((sl&0xFFFFFFFF00000000ULL)==tagv){
                    const EvRec*k=kept[idx-1];
                    if(k->h==r.h&&sv(k->idp,k->idl)==sv(r.idp,r.idl)){
                        if(r.ing<k->ing)kept[idx-1]=&r;
                        break;
                    }
                }
                i=(i+1)&mask;
            }
        }
        const size_t PF=8;
        for(size_t j=0;j<kept.size();j++){
            if(j+2*PF<kept.size())__builtin_prefetch(kept[j+2*PF]);
            if(j+PF<kept.size()){
                const EvRec*q=kept[j+PF];
                __builtin_prefetch(&lm.t[q->uh&lm.mask]);
            }
            const EvRec&r=*kept[j];
            if(r.ul==0||r.ts==BAD||r.ts<WS||r.ts>=WE)continue;
            if(r.type==0||r.pe)continue;
            if(r.type==3&&r.price<1)continue;
            if(r.ing-r.ts>DAY)continue;
            int32_t price=r.type==3?r.price:0;
            uint8_t cat=r.type==3?r.cat:0;
            sv name(r.up,r.ul);
            uint64_t nh=r.uh;
            uint32_t li=linkFind(name,nh);
            if(li!=UINT32_MAX){
                name=root[li].name;
                nh=root[li].h;
            }
            pushR(eb[tid][shardH(nh,NB)],EvB{name,nh,r.ts,price,cat},perEb);
        }
    });
    lap("B dedup+route");

    // ---- clicks: parallel parse and route ----
    vector<Chunk> cchunks;
    addChunks(listcsv(DATA+"/clicks"),cchunks,4<<20);
    vector<vector<vector<CkB>>> cb(NT,vector<vector<CkB>>(NB));
    size_t perCb=estRows(cchunks)/((size_t)NT*NB)*5/4+32;
    pfor(cchunks.size(),[&](size_t ci,int tid){
        rows(cchunks[ci],4,[&](sv*f){
            if(f[0].empty()||f[1].empty())return;
            uint32_t c=cm.find(f[2],hsh(f[2]));
            if(c==UINT32_MAX)return;
            ll ts=parse_ts(f[3]);
            if(ts==BAD||ts<WS-WEEK||ts>=WE)return;
            sv name=f[1];
            uint64_t nh=hsh(name);
            uint32_t li=linkFind(name,nh);
            if(li!=UINT32_MAX){
                name=root[li].name;
                nh=root[li].h;
            }
            pushR(cb[tid][shardH(nh,NB)],CkB{name,nh,ts,c,f[0]},perCb);
        });
    });
    lap("clicks");

    // ---- phase C: per bucket group by person (counting sort) + sweep ----
    vector<Out> outs(NB);
    vector<vector<CA>> tcam(NT,vector<CA>((size_t)(ncamp+1)*ND));
    vector<Scr> scr(NT);
    pfor(NB,[&](size_t b,int tid){
        CA*cam=tcam[tid].data();
        Scr&S=scr[tid];
        HM pl;
        size_t n=0,nc=0;
        for(int t=0;t<NT;t++){n+=eb[t][b].size();nc+=cb[t][b].size();}
        S.pe.resize(n);
        S.pc.resize(nc);
        vector<uint32_t>&pe_=S.pe;
        vector<uint32_t>&pc_=S.pc;
        {
            size_t k=0;
            for(int t=0;t<NT;t++)for(const EvB&x:eb[t][b])pe_[k++]=pl.add(x.nh,x.name);
            k=0;
            for(int t=0;t<NT;t++)for(const CkB&x:cb[t][b])pc_[k++]=pl.add(x.nh,x.name);
        }
        uint32_t np=(uint32_t)pl.names.size();
        S.es.assign(np+1,0);
        S.cs.assign(np+1,0);
        vector<uint32_t>&es=S.es;
        vector<uint32_t>&cs=S.cs;
        for(size_t k=0;k<n;k++)es[pe_[k]+1]++;
        for(size_t k=0;k<nc;k++)cs[pc_[k]+1]++;
        for(uint32_t p=0;p<np;p++){es[p+1]+=es[p];cs[p+1]+=cs[p];}
        S.ev.resize(n);
        S.ck.resize(nc);
        vector<E3>&ev=S.ev;
        vector<K3>&ck=S.ck;
        {
            S.ep.assign(es.begin(),es.end()-1);
            S.cp.assign(cs.begin(),cs.end()-1);
            size_t k=0;
            for(int t=0;t<NT;t++)for(const EvB&x:eb[t][b])ev[S.ep[pe_[k++]]++]=E3{x.ts,x.price,x.cat};
            k=0;
            for(int t=0;t<NT;t++)for(const CkB&x:cb[t][b])ck[S.cp[pc_[k++]]++]=K3{x.ts,x.camp,x.id};
        }
        for(uint32_t p=0;p<np;p++){
            sortRange(ev.data()+es[p],es[p+1]-es[p],[](const E3&a,const E3&c){return a.ts<c.ts;});
            sortRange(ck.data()+cs[p],cs[p+1]-cs[p],[](const K3&a,const K3&c){
                if(a.ts!=c.ts)return a.ts<c.ts;
                return a.id<c.id;
            });
        }

        Out&o=outs[b];
        vector<int> cov(ncamp,-1);
        vector<uint32_t> touched;
        vector<ll> sst;
        vector<int> slot,dd,dlo,dhi;
        vector<ll> daycat(ncat,0);
        ll curday=-1,dsess=0,devs=0,drev=0;
        auto flush=[&](uint32_t p){
            if(curday<0)return;
            int best=-1;
            ll mx=0;
            for(int k=0;k<ncat;k++)if(daycat[k]>mx){mx=daycat[k];best=k;}
            o.ud.push_back({pl.names[p],(int32_t)curday,dsess,devs,drev,best});
            fill(daycat.begin(),daycat.end(),0);
            curday=-1;dsess=0;devs=0;drev=0;
        };

        for(uint32_t p=0;p<np;p++){
            size_t i=es[p],ie=es[p+1];
            if(i==ie)continue;
            size_t cbp=cs[p],ce=cs[p+1];
            size_t lo=cbp,hi=cbp;
            sst.clear();slot.clear();dd.clear();dlo.clear();dhi.clear();
            size_t e=i;
            while(e<ie){
                ll s=ev[e].ts;
                ll day=s/DAY;
                if(day!=curday){flush(p);curday=day;}
                ll prev=s,rev=0;
                bool hp=false;
                size_t f=e;
                while(f<ie&&ev[f].ts-prev<=GAP){
                    prev=ev[f].ts;
                    if(ev[f].price){rev+=ev[f].price;hp=true;daycat[ev[f].cat]+=ev[f].price;}
                    f++;
                }
                dsess++;devs+=(ll)(f-e);drev+=rev;
                while(hi<ce&&ck[hi].ts<=s)hi++;
                while(lo<hi&&ck[lo].ts<s-WEEK)lo++;
                uint32_t camp=organic;
                if(lo<hi){
                    size_t last=hi-1,j=last;
                    while(j>lo&&ck[j-1].ts==ck[last].ts)j--;
                    camp=ck[j].camp;
                }
                int di=(int)(day-D0);
                CA&a=cam[(size_t)camp*ND+di];
                a.s++;
                if(hp)a.ps++;
                a.rev+=rev;
                int idx=(int)sst.size();
                sst.push_back(s);
                if(dd.empty()||dd.back()!=di){dd.push_back(di);dlo.push_back(idx);dhi.push_back(idx);}
                else dhi.back()=idx;
                slot.push_back((int)dd.size()-1);
                e=f;
            }
            flush(p);
            // assists: sessions with start in [click.ts, click.ts+WEEK]
            size_t m=sst.size(),pl_=0,pr=0;
            for(size_t k=cbp;k<ce;k++){
                ll t=ck[k].ts;
                while(pl_<m&&sst[pl_]<t)pl_++;
                while(pr<m&&sst[pr]<=t+WEEK)pr++;
                int l=(int)pl_,r=(int)pr-1;
                if(l>r)continue;
                uint32_t c=ck[k].camp;
                int l2=max(l,cov[c]+1);
                if(l2>r)continue;
                if(cov[c]<0)touched.push_back(c);
                cov[c]=r;
                for(int sl=slot[l2];sl<=slot[r];sl++){
                    int a=max(l2,dlo[sl]),z=min(r,dhi[sl]);
                    cam[(size_t)c*ND+dd[sl]].as+=z-a+1;
                }
            }
            for(uint32_t c:touched)cov[c]=-1;
            touched.clear();
        }
    });
    lap("C group+sweep");

    // ---- output: write Parquet directly ----
    mkdir(OUT.c_str(),0755);
    mkdir((OUT+"/user_daily").c_str(),0755);
    mkdir((OUT+"/campaign_daily").c_str(),0755);

    vector<thread> wt;
    for(int w=0;w<NT;w++)wt.emplace_back([&,w]{
        vector<Col> c(6);
        c[0]={"user_id",6,0,false};
        c[1]={"day",1,6,false};
        c[2]={"sessions",2,-1,false};
        c[3]={"events",2,-1,false};
        c[4]={"revenue_cents",2,-1,false};
        c[5]={"top_category",6,0,true};
        ll n=0;
        for(size_t b=w;b<(size_t)NB;b+=NT)for(const UdRow&r:outs[b].ud){
            c[0].as(r.name);
            c[1].a32(r.day);
            c[2].a64(r.s);
            c[3].a64(r.e);
            c[4].a64(r.r);
            if(r.cat>=0){c[5].as(catnames[r.cat]);c[5].defd.push_back(1);}
            else c[5].defd.push_back(0);
            n++;
        }
        if(n||w==0)writeParquet(OUT+"/user_daily/part-"+to_string(w)+".parquet",c,n);
    });

    {
        vector<CA> cam((size_t)(ncamp+1)*ND);
        for(int t=0;t<NT;t++)for(size_t k=0;k<cam.size();k++){
            CA&a=cam[k];
            const CA&x=tcam[t][k];
            a.s+=x.s;a.ps+=x.ps;a.rev+=x.rev;a.as+=x.as;
        }
        vector<Col> c(7);
        c[0]={"campaign_id",6,0,false};
        c[1]={"channel",6,0,false};
        c[2]={"day",1,6,false};
        c[3]={"sessions",2,-1,false};
        c[4]={"purchasing_sessions",2,-1,false};
        c[5]={"revenue_cents",2,-1,false};
        c[6]={"assisted_sessions",2,-1,false};
        ll n=0;
        for(size_t k=0;k<cam.size();k++){
            const CA&a=cam[k];
            if(!a.s&&!a.as)continue;
            uint32_t ci=(uint32_t)(k/ND);
            c[0].as(ci==organic?sv("organic"):campid[ci]);
            c[1].as(ci==organic?sv("organic"):campch[ci]);
            c[2].a32((int32_t)(D0+(ll)(k%ND)));
            c[3].a64(a.s);
            c[4].a64(a.ps);
            c[5].a64(a.rev);
            c[6].a64(a.as);
            n++;
        }
        writeParquet(OUT+"/campaign_daily/part-0.parquet",c,n);
    }
    for(auto&t:wt)t.join();
    lap("write");
    fflush(stderr);
    _exit(0);
}