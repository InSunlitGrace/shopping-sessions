#include <bits/stdc++.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
using namespace std;
typedef string_view sv;
typedef long long ll;

static const ll BAD=LLONG_MIN;
static const ll DAY=86400000LL;
static const ll WEEK=7*DAY;
static const ll GAP=1800000LL;

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

static bool split(const char*p,const char*e,int nc,sv*out){
    int k=0;
    while(true){
        const char*c=p;
        while(c<e&&*c!=',')c++;
        if(k>=nc)return false;
        const char*a=p;
        const char*b=c;
        if(b>a&&*a=='"'){
            if(b-a<2||b[-1]!='"')return false;
            a++;b--;
        }
        if(b>a&&memchr(a,'"',b-a))return false;
        out[k++]=sv(a,b-a);
        if(c==e)break;
        p=c+1;
    }
    return k==nc;
}

template<class F> static void eachrow(const vector<string>&paths,int nc,F f){
    for(const string&path:paths){
        int fd=open(path.c_str(),O_RDONLY);
        if(fd<0)continue;
        struct stat st;
        fstat(fd,&st);
        if(st.st_size==0){close(fd);continue;}
        const char*p=(const char*)mmap(0,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);
        close(fd);
        const char*e=p+st.st_size;
        const char*nl=(const char*)memchr(p,'\n',e-p);
        if(!nl)continue;
        p=nl+1;
        sv fields[8];
        while(p<e){
            const char*q=(const char*)memchr(p,'\n',e-p);
            const char*le=q?q:e;
            if(split(p,le,nc,fields))f(fields);
            p=q?q+1:e;
        }
    }
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
static void civil(ll z,int&y,int&m,int&d){
    z+=719468;
    ll era=(z>=0?z:z-146096)/146097;
    ll doe=z-era*146097;
    ll yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    ll yy=yoe+era*400;
    ll doy=doe-(365*yoe+yoe/4-yoe/100);
    ll mp=(5*doy+2)/153;
    d=(int)(doy-(153*mp+2)/5+1);
    m=(int)(mp<10?mp+3:mp-9);
    y=(int)(yy+(m<=2));
}

static ll parse_ts(sv s){
    size_t n=s.size();
    if(n==0)return BAD;
    bool all=true;
    for(size_t k=0;k<n;k++)if(!dg(s[k])){all=false;break;}
    if(all){
        size_t k=0;
        while(k<n-1&&s[k]=='0')k++;
        if(n-k>18)return BAD;
        ll v=0;
        for(;k<n;k++)v=v*10+(s[k]-'0');
        return v;
    }
    if(n!=20&&n!=25)return BAD;
    const char*p=s.data();
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
    int y=num(p,4),mo=num(p+5,2),d=num(p+8,2),h=num(p+11,2),mi=num(p+14,2),se=num(p+17,2);
    if(mo<1||mo>12||d<1||h>23||mi>59||se>59)return BAD;
    static const int dim[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int mx=dim[mo-1];
    if(mo==2&&((y%4==0&&y%100!=0)||y%400==0))mx=29;
    if(d>mx)return BAD;
    ll days=days_from_civil(y,mo,d);
    return ((days*86400LL+h*3600LL+mi*60LL+se)*1000LL)-off;
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

struct Interner{
    unordered_map<sv,uint32_t> m;
    vector<sv> names;
    uint32_t get(sv s){
        auto r=m.emplace(s,(uint32_t)names.size());
        if(r.second)names.push_back(s);
        return r.first->second;
    }
};
static Interner I;
static vector<uint32_t> par;
static uint32_t findr(uint32_t x){
    while(par[x]!=x){
        par[x]=par[par[x]];
        x=par[x];
    }
    return x;
}
static uint32_t person(sv s){
    uint32_t i=I.get(s);
    while(par.size()<I.names.size())par.push_back((uint32_t)par.size());
    return findr(i);
}

struct EvRec{sv user;ll ts;ll ing;sv prod;int type;ll price;};
struct Ev{uint32_t p;ll ts;int32_t price;uint8_t cat;};
struct Ck{uint32_t p;ll ts;uint32_t camp;sv id;};
struct CA{ll s=0,ps=0,rev=0,as=0;};

int main(){
    const char*e1=getenv("DATA_DIR");
    const char*e2=getenv("SCRATCH_DIR");
    string DATA=e1?e1:"/data";
    string SCR=e2?e2:"/tmp";

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

    set<sv> catset;
    vector<pair<sv,sv>> prows;
    eachrow({DATA+"/products.csv"},2,[&](sv*f){
        catset.insert(f[1]);
        prows.push_back({f[0],f[1]});
    });
    catset.insert(sv("unknown"));
    vector<sv> catnames(catset.begin(),catset.end());
    unordered_map<sv,uint8_t> catid;
    for(size_t k=0;k<catnames.size();k++)catid[catnames[k]]=(uint8_t)k;
    uint8_t unk=catid[sv("unknown")];
    unordered_map<sv,uint8_t> prodcat;
    for(auto&pr:prows)prodcat[pr.first]=catid[pr.second];
    int ncat=(int)catnames.size();

    unordered_map<sv,uint32_t> campidx;
    vector<sv> campid,campch;
    eachrow({DATA+"/campaigns.csv"},2,[&](sv*f){
        campidx[f[0]]=(uint32_t)campid.size();
        campid.push_back(f[0]);
        campch.push_back(f[1]);
    });
    uint32_t ncamp=(uint32_t)campid.size();
    uint32_t organic=ncamp;

    {
        vector<pair<uint32_t,uint32_t>> edges;
        eachrow(listcsv(DATA+"/links"),2,[&](sv*f){
            if(f[0].empty()||f[1].empty())return;
            uint32_t a=I.get(f[0]);
            uint32_t b=I.get(f[1]);
            edges.push_back({a,b});
        });
        while(par.size()<I.names.size())par.push_back((uint32_t)par.size());
        for(auto&ed:edges){
            uint32_t ra=findr(ed.first);
            uint32_t rb=findr(ed.second);
            if(ra==rb)continue;
            if(I.names[ra]<I.names[rb])par[rb]=ra;
            else par[ra]=rb;
        }
    }

    vector<Ev> ev;
    {
        vector<EvRec> recs;
        unordered_map<sv,uint32_t> eidx;
        eachrow(listcsv(DATA+"/events"),7,[&](sv*f){
            if(f[0].empty())return;
            ll ing=parse_ts(f[3]);
            if(ing==BAD)ing=LLONG_MAX;
            auto r=eidx.emplace(f[0],(uint32_t)recs.size());
            if(!r.second&&ing>=recs[r.first->second].ing)return;
            int type=0;
            if(f[4]=="view")type=1;
            else if(f[4]=="cart")type=2;
            else if(f[4]=="purchase")type=3;
            EvRec rec{f[1],parse_ts(f[2]),ing,f[5],type,type==3?parse_price(f[6]):-1};
            if(r.second)recs.push_back(rec);
            else recs[r.first->second]=rec;
        });
        ev.reserve(recs.size());
        for(auto&r:recs){
            if(r.user.empty()||r.ts==BAD||r.ts<WS||r.ts>=WE)continue;
            if(r.type==0||r.prod.empty())continue;
            if(r.type==3&&r.price<1)continue;
            if(r.ing-r.ts>DAY)continue;
            uint32_t p=person(r.user);
            int32_t price=0;
            uint8_t cat=0;
            if(r.type==3){
                price=(int32_t)r.price;
                auto it=prodcat.find(r.prod);
                cat=it==prodcat.end()?unk:it->second;
            }
            ev.push_back({p,r.ts,price,cat});
        }
    }
    sort(ev.begin(),ev.end(),[](const Ev&a,const Ev&b){
        if(a.p!=b.p)return a.p<b.p;
        return a.ts<b.ts;
    });

    vector<Ck> ck;
    eachrow(listcsv(DATA+"/clicks"),4,[&](sv*f){
        if(f[0].empty()||f[1].empty())return;
        auto c=campidx.find(f[2]);
        if(c==campidx.end())return;
        ll ts=parse_ts(f[3]);
        if(ts==BAD||ts<WS-WEEK||ts>=WE)return;
        ck.push_back({person(f[1]),ts,c->second,f[0]});
    });
    sort(ck.begin(),ck.end(),[](const Ck&a,const Ck&b){
        if(a.p!=b.p)return a.p<b.p;
        if(a.ts!=b.ts)return a.ts<b.ts;
        return a.id<b.id;
    });

    FILE*fu=fopen((SCR+"/ud.txt").c_str(),"wb");
    static char fbuf[1<<20];
    setvbuf(fu,fbuf,_IOFBF,sizeof fbuf);
    unordered_map<uint64_t,CA> cam;
    vector<int> cnt(ncamp+1,0),pos(ncamp+1,0);
    vector<uint32_t> active;
    auto addc=[&](uint32_t c){
        if(cnt[c]++==0){pos[c]=(int)active.size();active.push_back(c);}
    };
    auto remc=[&](uint32_t c){
        if(--cnt[c]==0){
            uint32_t last=active.back();
            active[pos[c]]=last;
            pos[last]=pos[c];
            active.pop_back();
        }
    };
    auto key=[](uint32_t c,ll day){return ((uint64_t)c<<32)|(uint64_t)day;};

    vector<ll> daycat(ncat,0);
    ll curday=-1,dsess=0,devs=0,drev=0;
    auto flush=[&](uint32_t p){
        if(curday<0)return;
        int best=-1;
        ll mx=0;
        for(int k=0;k<ncat;k++)if(daycat[k]>mx){mx=daycat[k];best=k;}
        int y,m,d;
        civil(curday,y,m,d);
        string nm(I.names[p]);
        for(char&c:nm)if(c=='\r')c=2;
        fprintf(fu,"%s\x01%04d-%02d-%02d\x01%lld\x01%lld\x01%lld\x01",nm.c_str(),y,m,d,dsess,devs,drev);
        if(best>=0)fwrite(catnames[best].data(),1,catnames[best].size(),fu);
        fputc('\n',fu);
        fill(daycat.begin(),daycat.end(),0);
        curday=-1;dsess=0;devs=0;drev=0;
    };

    size_t cb=0;
    size_t i=0;
    while(i<ev.size()){
        uint32_t p=ev[i].p;
        size_t ie=i;
        while(ie<ev.size()&&ev[ie].p==p)ie++;
        while(cb<ck.size()&&ck[cb].p<p)cb++;
        size_t ce=cb;
        while(ce<ck.size()&&ck[ce].p==p)ce++;
        size_t lo=cb,hi=cb;
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
            while(hi<ce&&ck[hi].ts<=s){addc(ck[hi].camp);hi++;}
            while(lo<hi&&ck[lo].ts<s-WEEK){remc(ck[lo].camp);lo++;}
            uint32_t camp=organic;
            if(lo<hi){
                size_t last=hi-1,j=last;
                while(j>lo&&ck[j-1].ts==ck[last].ts)j--;
                camp=ck[j].camp;
            }
            CA&a=cam[key(camp,day)];
            a.s++;
            if(hp)a.ps++;
            a.rev+=rev;
            for(uint32_t c:active)cam[key(c,day)].as++;
            e=f;
        }
        flush(p);
        for(size_t k=lo;k<hi;k++)remc(ck[k].camp);
        cb=ce;
        i=ie;
    }
    fclose(fu);

    FILE*fc=fopen((SCR+"/cd.txt").c_str(),"wb");
    for(auto&kv:cam){
        uint32_t c=(uint32_t)(kv.first>>32);
        ll day=(ll)(kv.first&0xffffffffULL);
        int y,m,d;
        civil(day,y,m,d);
        string id=c==organic?"organic":string(campid[c]);
        string ch=c==organic?"organic":string(campch[c]);
        fprintf(fc,"%s\x01%s\x01%04d-%02d-%02d\x01%lld\x01%lld\x01%lld\x01%lld\n",
            id.c_str(),ch.c_str(),y,m,d,kv.second.s,kv.second.ps,kv.second.rev,kv.second.as);
    }
    fclose(fc);
    return 0;
}