/* SPDX-License-Identifier: GPL-3.0-only */
#include "Bindings.h"

static const TsmHost* BH=0;
static InputApi BI={};

enum PrimaryType { PRIMARY_NONE=0, PRIMARY_KEY=1, PRIMARY_MOUSE=2 };
struct Modifier { int a,b; };
struct Binding
{
    int valid, disabled, type, code, modifierCount;
    Modifier modifiers[3];
    char text[64];
    int wasDown;
};
static Binding BOverlay={},BForce={},BGrid={},BAdd={},BRemove={};
static int BEscapeWasDown=0;

static char Upper(char c){ return (c>='a'&&c<='z')?(char)(c-32):c; }
static int Eq(const char* a,const char* b)
{
    if(!a||!b)return 0; while(*a&&*b){ if(*a++!=*b++)return 0; } return *a==0&&*b==0;
}
static int Starts(const char* a,const char* b)
{
    if(!a||!b)return 0; while(*b){ if(*a++!=*b++)return 0; } return 1;
}
static void Copy(char* d,int cap,const char* s)
{
    if(!d||cap<=0)return; int i=0; if(s) for(;s[i]&&i<cap-1;++i)d[i]=s[i]; d[i]=0;
}
static int ParseUnsigned(const char* s,int* out)
{
    if(!s||!*s||!out)return 0; int v=0; for(int i=0;s[i];++i){ if(s[i]<'0'||s[i]>'9')return 0; v=v*10+(s[i]-'0'); if(v>999)return 0; } *out=v; return 1;
}

static int LetterDik(char c)
{
    switch(c){
    case 'A':return 30; case 'B':return 48; case 'C':return 46; case 'D':return 32;
    case 'E':return 18; case 'F':return 33; case 'G':return 34; case 'H':return 35;
    case 'I':return 23; case 'J':return 36; case 'K':return 37; case 'L':return 38;
    case 'M':return 50; case 'N':return 49; case 'O':return 24; case 'P':return 25;
    case 'Q':return 16; case 'R':return 19; case 'S':return 31; case 'T':return 20;
    case 'U':return 22; case 'V':return 47; case 'W':return 17; case 'X':return 45;
    case 'Y':return 21; case 'Z':return 44; default:return 0; }
}
static int NumberDik(char c)
{
    static const int codes[10]={11,2,3,4,5,6,7,8,9,10};
    return (c>='0'&&c<='9')?codes[c-'0']:0;
}
static int ParseModifier(const char* t,Modifier* m)
{
    if(!t||!m)return 0;
    if(Eq(t,"CTRL")){m->a=29;m->b=157;return 1;}
    if(Eq(t,"LCTRL")){m->a=29;m->b=0;return 1;}
    if(Eq(t,"RCTRL")){m->a=157;m->b=0;return 1;}
    if(Eq(t,"SHIFT")){m->a=42;m->b=54;return 1;}
    if(Eq(t,"LSHIFT")){m->a=42;m->b=0;return 1;}
    if(Eq(t,"RSHIFT")){m->a=54;m->b=0;return 1;}
    if(Eq(t,"ALT")){m->a=56;m->b=184;return 1;}
    if(Eq(t,"LALT")||Eq(t,"LEFTALT")){m->a=56;m->b=0;return 1;}
    if(Eq(t,"RALT")||Eq(t,"RIGHTALT")){m->a=184;m->b=0;return 1;}
    return 0;
}
static int NamedDik(const char* t)
{
    struct Pair{const char* n;int v;};
    static const Pair p[]={
        {"ESC",1},{"ESCAPE",1},{"TAB",15},{"ENTER",28},{"RETURN",28},{"SPACE",57},
        {"CAPSLOCK",58},{"BACKSPACE",14},{"MINUS",12},{"EQUALS",13},{"LBRACKET",26},
        {"RBRACKET",27},{"BACKSLASH",43},{"SEMICOLON",39},{"APOSTROPHE",40},{"GRAVE",41},
        {"COMMA",51},{"PERIOD",52},{"SLASH",53},{"HOME",199},{"END",207},{"PAGEUP",201},
        {"PAGEDOWN",209},{"INSERT",210},{"DELETE",211},{"UP",200},{"DOWN",208},{"LEFT",203},
        {"RIGHT",205},{"NUMLOCK",69},{"SCROLLLOCK",70},{"LWIN",219},{"RWIN",220},
        {"NUMPADENTER",156},{"NUMPADPLUS",78},{"NUMPADMINUS",74},{"NUMPADSTAR",55},
        {"NUMPADSLASH",181},{"NUMPADPERIOD",83},{"LEFTSHIFT",42},{"RIGHTSHIFT",54},
        {"LEFTCTRL",29},{"RIGHTCTRL",157}
    };
    for(unsigned i=0;i<sizeof(p)/sizeof(p[0]);++i)if(Eq(t,p[i].n))return p[i].v;
    if(Starts(t,"NUMPAD")&&strlen(t)==7){ char c=t[6]; if(c>='0'&&c<='9'){ static const int n[10]={82,79,80,81,75,76,77,71,72,73}; return n[c-'0']; }}
    if(t[0]=='F'){
        int n=0; if(ParseUnsigned(t+1,&n)&&n>=1&&n<=12) return n<=10?58+n:(n==11?87:88);
    }
    return 0;
}
static int ParsePrimary(const char* t,int* type,int* code)
{
    if(!t||!type||!code)return 0;
    if(Eq(t,"MOUSE1")||Eq(t,"LMB")){*type=PRIMARY_MOUSE;*code=1;return 1;}
    if(Eq(t,"MOUSE2")||Eq(t,"RMB")){*type=PRIMARY_MOUSE;*code=2;return 1;}
    if(Eq(t,"MOUSE4")||Eq(t,"M4")||Eq(t,"X1")){*type=PRIMARY_MOUSE;*code=4;return 1;}
    if(Eq(t,"MOUSE5")||Eq(t,"M5")||Eq(t,"X2")){*type=PRIMARY_MOUSE;*code=5;return 1;}
    int d=0;
    if(t[0]&&t[1]==0){ d=LetterDik(t[0]); if(!d)d=NumberDik(t[0]); }
    if(!d)d=NamedDik(t);
    if(d){*type=PRIMARY_KEY;*code=d;return 1;}
    return 0;
}
static int ParseBinding(const char* src,Binding* out)
{
    if(!src||!out)return 0; memset(out,0,sizeof(*out));
    char norm[64]; int n=0;
    for(int i=0;src[i]&&n<63;++i){ char c=src[i]; if(c==' '||c=='\t'||c=='\r'||c=='\n')continue; norm[n++]=Upper(c); }
    norm[n]=0; Copy(out->text,64,norm);
    if(!norm[0])return 0;
    if(Eq(norm,"NONE")){out->valid=1;out->disabled=1;return 1;}
    char work[64];Copy(work,64,norm); char* token=work;
    for(int i=0;;++i){ char c=work[i]; if(c=='+'||c==0){work[i]=0;if(!*token)return 0; Modifier m={};int ty=0,co=0;
        if(ParseModifier(token,&m)){if(out->modifierCount>=3||out->type)return 0;out->modifiers[out->modifierCount++]=m;}
        else if(ParsePrimary(token,&ty,&co)){if(out->type)return 0;out->type=ty;out->code=co;}
        else return 0; if(c==0)break;token=work+i+1;}}
    if(!out->type)return 0;out->valid=1;return 1;
}
static void Load(const char* key,const char* fallback,Binding* b)
{
    char value[64]={}; BH->configString("plugins\\ForceNodes.ini","ForceNodes",key,value,64,fallback);
    if(ParseBinding(value,b))return; BH->log("ForceNodes  invalid binding %s=%s; using %s",key,value,fallback); ParseBinding(fallback,b);
}
static int KeyDownRaw(int dik)
{
    return BI.object&&BI.keyDown&&dik>0&&dik<256&&BI.keyDown(BI.object,dik)?1:0;
}
static int MouseDownRaw(int code)
{
    if(!BI.object)return 0;
    C3DMousePressFn fn=0;
    if(code==1)fn=BI.mouseLeft; else if(code==2)fn=BI.mouseRight; else if(code==4)fn=BI.mouseX1; else if(code==5)fn=BI.mouseX2;
    return fn&&fn(BI.object)?1:0;
}
static int ModifierDown(const Modifier& m){return KeyDownRaw(m.a)||(m.b&&KeyDownRaw(m.b));}
static int IsDown(const Binding& b)
{
    if(!b.valid||b.disabled||!BI.object)return 0;
    for(int i=0;i<b.modifierCount;++i)if(!ModifierDown(b.modifiers[i]))return 0;
    return b.type==PRIMARY_KEY?KeyDownRaw(b.code):MouseDownRaw(b.code);
}
static int Pressed(Binding& b){int d=IsDown(b);int p=d&&!b.wasDown;b.wasDown=d;return p;}

int Bindings_Init(const TsmHost* host,const InputApi* input)
{
    if(!host||!input||!input->keyDown||!input->mouseX1||!input->mouseX2)return 0;
    BH=host;BI=*input;
    Load("bind_overlay","CTRL+NUMPAD8",&BOverlay);
    Load("bind_force","CTRL+NUMPAD9",&BForce);
    Load("bind_grid","CTRL+NUMPAD0",&BGrid);
    Load("bind_add_node","MOUSE4",&BAdd);
    Load("bind_remove_node","MOUSE5",&BRemove);
    BH->log("ForceNodes  bindings overlay=%s force=%s grid=%s add=%s remove=%s",BOverlay.text,BForce.text,BGrid.text,BAdd.text,BRemove.text);
    return 1;
}
void Bindings_SetInputObject(void* o){BI.object=o;}
void Bindings_Poll(BindingEvents* e)
{
    if(!e)return; memset(e,0,sizeof(*e));
    e->overlayPressed=Pressed(BOverlay); e->forcePressed=Pressed(BForce); e->gridPressed=Pressed(BGrid);
    e->addPressed=Pressed(BAdd); e->removePressed=Pressed(BRemove);
    int d=KeyDownRaw(1);e->escapePressed=d&&!BEscapeWasDown;BEscapeWasDown=d;
}
int Bindings_KeyDown(int dik){return KeyDownRaw(dik);}
const char* Bindings_OverlayText(void){return BOverlay.text;}
const char* Bindings_ForceText(void){return BForce.text;}
const char* Bindings_GridText(void){return BGrid.text;}
const char* Bindings_AddText(void){return BAdd.text;}
const char* Bindings_RemoveText(void){return BRemove.text;}
