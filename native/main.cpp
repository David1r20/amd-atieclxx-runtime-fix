// AMD atieclxx native portable utility (experimental).
// No .NET / PowerShell. Uses only Windows system APIs.
// Supports diagnosis for signed AMD clients; patch only a pinned SHA256 and opcode.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <wintrust.h>
#include <softpub.h>
#include <winver.h>
#include <bcrypt.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <cmath>
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"crypt32.lib")
#pragma comment(lib,"wintrust.lib")
#pragma comment(lib,"version.lib")
#pragma comment(lib,"pdh.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"shlwapi.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"user32.lib")

static const wchar_t* SERVICE=L"AMD External Events Utility";
static const wchar_t* TASK=L"AMD-atieclxx-NativeFix";
static const wchar_t* EXPECTED_HASH=L"046C13260AEA96D5B432A40A407F2D4B94D5333B9BDA2AA6E1B381C15D03B066";
static const DWORD RVA=0x21122;
static const BYTE OLD_BYTES[6]={0xFF,0x15,0x78,0xA5,0x04,0x00};
static const BYTE NEW_BYTES[6]={0xB8,0x01,0,0,0,0x90};
static const wchar_t* OLD_MUTEX=L"Global\\AmdAtieclxxRuntimeFix";
static const wchar_t* WATCH_MUTEX=L"Global\\AtieclxxNativeFixWatch";
static const int ID_REFRESH=101, ID_TEST=102, ID_ENABLE=103, ID_DISABLE=104;
static const UINT WM_DIAGNOSTIC=WM_APP+7;
static HWND gWnd=nullptr, gRefresh=nullptr, gTest=nullptr, gEnable=nullptr, gDisable=nullptr;
static HFONT gFont=nullptr,gFontTitle=nullptr,gFontSmall=nullptr;
static bool gWorking=false;
static const COLORREF BG=RGB(14,19,26),PANEL=RGB(23,31,41);
static const COLORREF WHITE=RGB(239,245,249),MUTED=RGB(158,176,188),ACCENT=RGB(120,218,208);
static const COLORREF WARN=RGB(241,188,110),RED=RGB(245,153,150);
struct State {
 std::wstring file,hash,version,serviceMode;
 bool validAMD=false, supported=false, exists=false, installed=false, legacy=false, serviceKnown=false, serviceRunning=false;
 DWORD pid=0; bool processMeasured=false,diskMeasured=false,historical=false;
 double processMBps=0,diskMBps=0; bool unsafe=false;
 std::wstring note, message;
};
static State gState;
static std::wstring Trim(std::wstring x){auto a=x.find_first_not_of(L" \r\n\t\""); if(a==std::wstring::npos)return L"";auto b=x.find_last_not_of(L" \r\n\t\""); return x.substr(a,b-a+1);}
static std::wstring Lower(std::wstring x){std::transform(x.begin(),x.end(),x.begin(),[](wchar_t c){return (wchar_t)towlower(c);});return x;}
static std::wstring GetSelfPath(){wchar_t b[32768]{};GetModuleFileNameW(nullptr,b,32768);return b;}
static bool FileExists(const std::wstring& p){DWORD a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);}
static std::wstring ParentDir(const std::wstring &p){size_t n=p.find_last_of(L"\\/");return n==std::wstring::npos?L"":p.substr(0,n);}
static std::wstring BaseName(const std::wstring& p){size_t n=p.find_last_of(L"\\/");return n==std::wstring::npos?p:p.substr(n+1);}
static std::wstring StrVersion(const std::wstring& p){
 DWORD dummy=0;DWORD n=GetFileVersionInfoSizeW(p.c_str(),&dummy);
 if(!n)return L"";
 std::vector<BYTE> data(n);if(!GetFileVersionInfoW(p.c_str(),0,n,data.data()))return L"";
 VS_FIXEDFILEINFO* fi=nullptr;UINT sz=0;
 if(VerQueryValueW(data.data(),L"\\",(LPVOID*)&fi,&sz)&&fi){
  wchar_t b[100]{};swprintf_s(b,L"%u.%u.%u.%u",HIWORD(fi->dwFileVersionMS),LOWORD(fi->dwFileVersionMS),HIWORD(fi->dwFileVersionLS),LOWORD(fi->dwFileVersionLS));return b;
 }
 return L"";
}
static std::wstring GetCompanyName(const std::wstring& p){
 DWORD dummy=0;DWORD n=GetFileVersionInfoSizeW(p.c_str(),&dummy);if(!n)return L"";
 std::vector<BYTE> data(n);if(!GetFileVersionInfoW(p.c_str(),0,n,data.data()))return L"";
 struct LANGANDCODEPAGE{WORD lang,codepage;};LANGANDCODEPAGE* tr=nullptr;UINT len=0;
 if(!VerQueryValueW(data.data(),L"\\VarFileInfo\\Translation",(LPVOID*)&tr,&len)||len<sizeof(LANGANDCODEPAGE))return L"";
 wchar_t query[160]{};swprintf_s(query,L"\\StringFileInfo\\%04x%04x\\CompanyName",tr->lang,tr->codepage);
 wchar_t* value=nullptr;if(VerQueryValueW(data.data(),query,(LPVOID*)&value,&len)&&value)return value;
 return L"";
}
static bool IsVendorSigned(const std::wstring& path){
 WINTRUST_FILE_INFO f{};f.cbStruct=sizeof(f);f.pcwszFilePath=path.c_str();
 GUID g=WINTRUST_ACTION_GENERIC_VERIFY_V2;
 WINTRUST_DATA d{};d.cbStruct=sizeof(d);d.dwUIChoice=WTD_UI_NONE;d.fdwRevocationChecks=WTD_REVOKE_NONE;
 d.dwUnionChoice=WTD_CHOICE_FILE;d.pFile=&f;d.dwStateAction=WTD_STATEACTION_VERIFY;
 d.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL|WTD_REVOCATION_CHECK_NONE;
 LONG result=WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,&g,&d);
 d.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,&g,&d);
 if(result!=ERROR_SUCCESS)return false;
 auto company=Lower(GetCompanyName(path));
 return company.find(L"amd")!=std::wstring::npos||company.find(L"advanced micro devices")!=std::wstring::npos;
}
static std::wstring Sha256(const std::wstring& file){
 HANDLE h=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h==INVALID_HANDLE_VALUE)return L"";
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD len=0,got=0;
 std::vector<BYTE> obj;BYTE digest[32]{};std::wstring out;
 do{
  if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)break;
  if(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&len,sizeof(len),&got,0)<0)break;
  obj.resize(len);
  if(BCryptCreateHash(alg,&hash,obj.data(),len,nullptr,0,0)<0)break;
  BYTE buffer[65536];DWORD count=0;bool ok=true;
  while(ReadFile(h,buffer,sizeof(buffer),&count,nullptr)&&count>0){if(BCryptHashData(hash,buffer,count,0)<0){ok=false;break;}}
  if(!ok||BCryptFinishHash(hash,digest,32,0)<0)break;
  wchar_t hex[65]{};
  for(int i=0;i<32;i++)swprintf_s(hex+2*i,65-2*i,L"%02X",digest[i]);
  out=hex;
 }while(false);
 if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);CloseHandle(h);return out;
}
static SC_HANDLE OpenServiceHandle(DWORD access){
 SC_HANDLE scm=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
 if(!scm)return nullptr;
 SC_HANDLE svc=OpenServiceW(scm,SERVICE,access);
 CloseServiceHandle(scm);return svc;
}
static bool QueryServiceState(bool& running,DWORD& start){
 SC_HANDLE svc=OpenServiceHandle(SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG);if(!svc)return false;
 SERVICE_STATUS_PROCESS status{};DWORD need=0;
 bool ok=QueryServiceStatusEx(svc,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&need)!=0;
 if(ok)running=status.dwCurrentState==SERVICE_RUNNING;
 need=0;QueryServiceConfigW(svc,nullptr,0,&need);
 std::vector<BYTE> b(need?need:8192);
 if(!QueryServiceConfigW(svc,(QUERY_SERVICE_CONFIGW*)b.data(),(DWORD)b.size(),&need))ok=false;
 else start=((QUERY_SERVICE_CONFIGW*)b.data())->dwStartType;
 CloseServiceHandle(svc);return ok;
}
static bool SetStart(DWORD mode){SC_HANDLE svc=OpenServiceHandle(SERVICE_CHANGE_CONFIG);if(!svc)return false;
 bool ok=ChangeServiceConfigW(svc,SERVICE_NO_CHANGE,mode,SERVICE_NO_CHANGE,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)!=0;
 CloseServiceHandle(svc);return ok;}
static bool StartAMD(){SC_HANDLE svc=OpenServiceHandle(SERVICE_START|SERVICE_QUERY_STATUS);if(!svc)return false;
 bool ok=StartServiceW(svc,0,nullptr)!=0||GetLastError()==ERROR_SERVICE_ALREADY_RUNNING;CloseServiceHandle(svc);return ok;}
static bool StopAMD(){SC_HANDLE svc=OpenServiceHandle(SERVICE_STOP|SERVICE_QUERY_STATUS);if(!svc)return false;
 SERVICE_STATUS status{};bool ok=ControlService(svc,SERVICE_CONTROL_STOP,&status)!=0||GetLastError()==ERROR_SERVICE_NOT_ACTIVE;CloseServiceHandle(svc);
 if(ok){for(int i=0;i<30;i++){bool running;DWORD mode;if(QueryServiceState(running,mode)&&!running)break;Sleep(100);}}
 return ok;}
static std::wstring ServiceBinary(){
 SC_HANDLE svc=OpenServiceHandle(SERVICE_QUERY_CONFIG);if(!svc)return L"";
 DWORD need=0;QueryServiceConfigW(svc,nullptr,0,&need);
 std::vector<BYTE> b(need?need:8192);
 std::wstring output;
 if(QueryServiceConfigW(svc,(QUERY_SERVICE_CONFIGW*)b.data(),(DWORD)b.size(),&need)){
  output=((QUERY_SERVICE_CONFIGW*)b.data())->lpBinaryPathName;
 }
 CloseServiceHandle(svc);
 if(output.empty())return L"";
 // Service binary path may be quoted or unquoted and have arguments.
 if(output[0]==L'"'){size_t n=output.find(L'"',1);if(n!=std::wstring::npos)return output.substr(1,n-1);}
 auto pos=Lower(output).find(L".exe"); if(pos!=std::wstring::npos)return Trim(output.substr(0,pos+4));
 return L"";
}
static DWORD FindPID(const wchar_t* name){
 HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(h==INVALID_HANDLE_VALUE)return 0;
 PROCESSENTRY32W e{};e.dwSize=sizeof(e);DWORD pid=0;
 if(Process32FirstW(h,&e))do{if(_wcsicmp(e.szExeFile,name)==0){pid=e.th32ProcessID;break;}}while(Process32NextW(h,&e));
 CloseHandle(h);return pid;
}
static bool CheckLegacyMonitor(){HANDLE h=OpenMutexW(SYNCHRONIZE,FALSE,OLD_MUTEX);if(h){CloseHandle(h);return true;}return FindPID(L"AmdAtieclxxRuntimeFix.exe")!=0;}
static bool CheckOwnMonitor(){HANDLE h=OpenMutexW(SYNCHRONIZE,FALSE,WATCH_MUTEX);if(h){CloseHandle(h);return true;}return false;}
static bool PidBytes(DWORD pid,ULONGLONG& bytes){
 HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!h)return false;
 IO_COUNTERS c{};bool ok=GetProcessIoCounters(h,&c)!=0;bytes=c.WriteTransferCount;CloseHandle(h);return ok;
}
struct DiskSample {PDH_HQUERY query=nullptr;PDH_HCOUNTER counter=nullptr;bool ready=false;
 DiskSample(){
  if(PdhOpenQueryW(nullptr,0,&query)!=ERROR_SUCCESS)return;
  if(PdhAddEnglishCounterW(query,L"\\PhysicalDisk(*)\\Disk Write Bytes/sec",0,&counter)!=ERROR_SUCCESS)return;
  ready=PdhCollectQueryData(query)==ERROR_SUCCESS;
 }
 bool Read(double& rate){
  if(!ready||PdhCollectQueryData(query)!=ERROR_SUCCESS)return false;
  DWORD size=0,count=0;
  PDH_STATUS st=PdhGetFormattedCounterArrayW(counter,PDH_FMT_DOUBLE,&size,&count,nullptr);
  if(st!=PDH_MORE_DATA||!size)return false;
  std::vector<BYTE> memory(size);
  PDH_FMT_COUNTERVALUE_ITEM_W* items=(PDH_FMT_COUNTERVALUE_ITEM_W*)memory.data();
  st=PdhGetFormattedCounterArrayW(counter,PDH_FMT_DOUBLE,&size,&count,items);
  if(st!=ERROR_SUCCESS)return false;
  for(DWORD i=0;i<count;i++){
   if(items[i].szName && wcsstr(items[i].szName,L"C:") && items[i].FmtValue.CStatus==PDH_CSTATUS_VALID_DATA){
     rate=items[i].FmtValue.doubleValue;return std::isfinite(rate)&&rate>=0;
   }
  }
  return false;
 }
 ~DiskSample(){if(query)PdhCloseQuery(query);}
};
static bool Measure(DWORD pid,DWORD ms,double& processMB,double& diskMB,bool& diskMeasured){
 ULONGLONG a=0,b=0;if(!PidBytes(pid,a))return false;
 DiskSample ds;ULONGLONG t0=GetTickCount64();Sleep(ms);double sec=(GetTickCount64()-t0)/1000.;
 if(!PidBytes(pid,b)||sec<=0)return false;
 processMB=(b>=a?(b-a)/sec:0.)/1000000.;
 double rate=0;diskMeasured=ds.Read(rate);diskMB=diskMeasured?rate/1000000.:0;
 return true;
}
static bool TaskExists(); // native scheduled task can be queried even if the SYSTEM mutex is inaccessible.
static State Inspect(bool doSample){
 State st;st.file=ServiceBinary();
 if(st.file.empty()||_wcsicmp(BaseName(st.file).c_str(),L"atiesrxx.exe")!=0){
  st.message=L"Serviço AMD não encontrado ou com executável inesperado.";return st;
 }
 st.file=ParentDir(st.file)+L"\\atieclxx.exe";st.exists=FileExists(st.file);
 if(!st.exists){st.message=L"Cliente atieclxx.exe não encontrado.";return st;}
 st.hash=Sha256(st.file);st.version=StrVersion(st.file);
 st.validAMD=IsVendorSigned(st.file);
 st.supported=st.validAMD&&_wcsicmp(st.hash.c_str(),EXPECTED_HASH)==0;
 DWORD mode=0;st.serviceKnown=QueryServiceState(st.serviceRunning,mode);
 st.serviceMode=!st.serviceKnown?L"Indisponível":mode==SERVICE_AUTO_START?L"Automático":mode==SERVICE_DISABLED?L"Desativado":L"Manual";
 st.pid=FindPID(L"atieclxx.exe");
 st.legacy=CheckLegacyMonitor();st.installed=CheckOwnMonitor() || TaskExists();
 if(!st.validAMD){st.message=L"Assinatura ou fornecedor AMD não validado. Diagnóstico bloqueado.";return st;}
 if(st.pid && doSample){
  st.processMeasured=Measure(st.pid,3000,st.processMBps,st.diskMBps,st.diskMeasured);
 }
 if(!st.serviceKnown)st.message=L"Não foi possível consultar o estado real do serviço AMD.";
 else if(!st.serviceRunning)st.message=st.installed||st.legacy?L"Monitor registrado, mas o serviço AMD está parado; proteção indisponível.":L"O serviço está parado. Um teste opcional pode verificar a falha original.";
 else if(!st.pid)st.message=L"Serviço em execução, mas o processo AMD ainda não foi localizado.";
 else if(st.installed||st.legacy)st.message=st.processMeasured&&st.processMBps<0.15?L"Serviço AMD em execução e sem escrita repetitiva na amostra.":L"Monitor registrado; confirme se a escrita repetitiva foi eliminada.";
 else if(st.processMeasured&&st.processMBps>=0.15)st.message=st.supported?L"Gravações excessivas detectadas. Correção disponível.":L"Gravações excessivas detectadas. SHA ainda sem patch validado.";
 else if(st.processMeasured)st.message=L"Sem gravações excessivas na amostra.";
 else st.message=L"Processo ativo, porém não foi possível medir o I/O.";
 return st;
}
static bool ReadBytes(HANDLE h,LPVOID addr,const BYTE* expected){
 BYTE b[6]{};SIZE_T got=0;return ReadProcessMemory(h,addr,b,6,&got)&&got==6&&!memcmp(b,expected,6);
}
typedef LONG (NTAPI* NtSuspendResume)(HANDLE);
static bool PatchPID(DWORD pid,bool& already){
 already=false;HANDLE h=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_SUSPEND_RESUME,FALSE,pid);
 if(!h)return false;
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
 if(snap==INVALID_HANDLE_VALUE){CloseHandle(h);return false;}
 MODULEENTRY32W m{};m.dwSize=sizeof(m);bool found=false;
 if(Module32FirstW(snap,&m))do{if(_wcsicmp(m.szModule,L"atieclxx.exe")==0){found=true;break;}}while(Module32NextW(snap,&m));
 CloseHandle(snap);
 if(!found){CloseHandle(h);return false;}
 BYTE* addr=(BYTE*)m.modBaseAddr+RVA;
 if(ReadBytes(h,addr,NEW_BYTES)){already=true;CloseHandle(h);return true;}
 if(!ReadBytes(h,addr,OLD_BYTES)){CloseHandle(h);return false;}
 HMODULE nt=GetModuleHandleW(L"ntdll.dll");auto suspend=(NtSuspendResume)GetProcAddress(nt,"NtSuspendProcess");
 auto resume=(NtSuspendResume)GetProcAddress(nt,"NtResumeProcess");
 bool ok=false;bool suspended=false,writable=false;DWORD oldProtect=0;
 if(suspend&&resume&&suspend(h)==0){
  suspended=true;
  if(ReadBytes(h,addr,OLD_BYTES)&&VirtualProtectEx(h,addr,6,PAGE_EXECUTE_READWRITE,&oldProtect)){
   writable=true;SIZE_T wrote=0;
   if(WriteProcessMemory(h,addr,NEW_BYTES,6,&wrote)&&wrote==6){
    FlushInstructionCache(h,addr,6);
    ok=ReadBytes(h,addr,NEW_BYTES);
   }
   if(!ok){WriteProcessMemory(h,addr,OLD_BYTES,6,&wrote);FlushInstructionCache(h,addr,6);}
  }
 }
 if(writable){DWORD dummy=0;VirtualProtectEx(h,addr,6,oldProtect,&dummy);}
 if(suspended)resume(h);
 CloseHandle(h);return ok;
}
static bool Apply(){
 State st=Inspect(false);
 if(!st.supported||!st.pid)return false;
 bool already=false;return PatchPID(st.pid,already);
}
static bool Watch(){
 HANDLE mutex=CreateMutexW(nullptr,TRUE,WATCH_MUTEX);
 if(!mutex)return false;
 if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mutex);return false;}
 DWORD lastPid=0;int attempts=0;std::wstring hash;
 while(true){
  DWORD pid=FindPID(L"atieclxx.exe");
  if(pid!=lastPid){lastPid=pid;attempts=0;hash=L"";}
  if(pid&&attempts<6){
   if(hash.empty()){
    std::wstring path=ServiceBinary();
    path=ParentDir(path)+L"\\atieclxx.exe";
    hash=Sha256(path);
   }
   if(_wcsicmp(hash.c_str(),EXPECTED_HASH)==0){
    bool already=false;if(PatchPID(pid,already))attempts=6;else attempts++;
   }else attempts=6; // Unsupported SHA: do not patch.
  }
  Sleep(300);
 }
}
// Windows-native scheduled task registration, no PowerShell or third-party utilities.
static bool RunTool(const std::wstring& exe,const std::wstring& args,DWORD timeout=12000){
 std::wstring cmd=L"\""+exe+L"\" "+args;
 std::vector<wchar_t> writable(cmd.begin(),cmd.end());writable.push_back(0);
 STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
 BOOL started=CreateProcessW(exe.c_str(),writable.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
 if(!started)return false;
 DWORD wr=WaitForSingleObject(pi.hProcess,timeout);
 if(wr==WAIT_TIMEOUT){TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,2000);}
 DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
 return wr==WAIT_OBJECT_0&&code==0;
}
static std::wstring SystemTool(){wchar_t path[32768]{};GetSystemDirectoryW(path,32768);return std::wstring(path)+L"\\schtasks.exe";}
static bool TaskExists(){
 return RunTool(SystemTool(),L"/Query /TN \""+std::wstring(TASK)+L"\" /FO LIST",4000);
}
static std::wstring InstalledDir(){
 wchar_t program[32768]{};
 DWORD count=GetEnvironmentVariableW(L"ProgramFiles",program,32768);
 if(!count||count>=32768)return L"";
 return std::wstring(program)+L"\\AMDAtieclxxNativeFix";
}
static std::wstring InstalledExe(){auto d=InstalledDir();return d.empty()?L"":d+L"\\AMDAtieclxxFix.exe";}
static bool IsAdmin(){
 BOOL admin=FALSE;SID_IDENTIFIER_AUTHORITY nt=SECURITY_NT_AUTHORITY;PSID sid=nullptr;
 if(AllocateAndInitializeSid(&nt,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&sid)){
  CheckTokenMembership(nullptr,sid,&admin);FreeSid(sid);
 }return admin!=0;
}
static bool EnableProtection(){
 if(!IsAdmin()||CheckLegacyMonitor()||TaskExists())return false;
 State st=Inspect(false);if(!st.supported)return false;
 std::wstring dir=InstalledDir(),dest=InstalledExe();if(dir.empty())return false;
 if(!CreateDirectoryW(dir.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
 if(!CopyFileW(GetSelfPath().c_str(),dest.c_str(),FALSE))return false;
 bool priorRunning=false;DWORD priorMode=0;if(!QueryServiceState(priorRunning,priorMode))return false;
 std::wstring cmd=L"/Create /SC ONSTART /TN \""+std::wstring(TASK)+L"\" /TR \"\\\""+dest+L"\\\" --watch\" /RU SYSTEM /RL HIGHEST /F";
 bool taskCreated=RunTool(SystemTool(),cmd,15000);
 if(!taskCreated)return false;
 bool ok=false;
 do{
  if(!RunTool(SystemTool(),L"/Run /TN \""+std::wstring(TASK)+L"\"",10000))break;
  Sleep(1100);
  if(!SetStart(SERVICE_AUTO_START))break;
  if(!StartAMD())break;
  // Startup is asynchronous: do not mistake a slow AMD client start for patch failure.
  DWORD pid=0;
  bool serviceReady=false;
  for(int i=0;i<40;i++){
   bool running=false;DWORD currentMode=0;
   serviceReady=QueryServiceState(running,currentMode)&&running;
   pid=FindPID(L"atieclxx.exe");
   if(serviceReady&&pid)break;
   Sleep(150);
  }
  if(!serviceReady||!pid||!Apply())break;
  double a=0,b=0;bool dm=false;
  if(!Measure(pid,2500,a,b,dm)||a>0.15)break;
  bool stillRunning=false;DWORD currentMode=0;
  if(!QueryServiceState(stillRunning,currentMode)||!stillRunning||!FindPID(L"atieclxx.exe"))break;
  ok=true;
 }while(false);
 if(!ok){
  RunTool(SystemTool(),L"/End /TN \""+std::wstring(TASK)+L"\"",6000);
  RunTool(SystemTool(),L"/Delete /TN \""+std::wstring(TASK)+L"\" /F",6000);
  StopAMD();SetStart(SERVICE_DISABLED); // Conservative fallback: do not leave an unpatched loop.
 }
 return ok;
}
static bool DisableProtection(){
 if(!IsAdmin())return false;
 RunTool(SystemTool(),L"/End /TN \""+std::wstring(TASK)+L"\"",6000);
 RunTool(SystemTool(),L"/Delete /TN \""+std::wstring(TASK)+L"\" /F",6000);
 bool ok=StopAMD()&&SetStart(SERVICE_DISABLED);
 return ok; // Keep native exe and logs; no Windows uninstaller needed.
}
static bool TemporaryTest(double& pRate,double& dRate,bool& dMeasured){
 if(!IsAdmin()||CheckLegacyMonitor()||TaskExists()||CheckOwnMonitor())return false;
 State st=Inspect(false);if(!st.validAMD)return false;
 bool wasRunning=false;DWORD mode=0;if(!QueryServiceState(wasRunning,mode))return false;
 bool changed=false,started=false,success=false;
 do{
  if(!wasRunning){
   if(mode==SERVICE_DISABLED){if(!SetStart(SERVICE_DEMAND_START))break;changed=true;}
   if(!StartAMD())break;started=true;
   Sleep(1000);
  }
  DWORD pid=FindPID(L"atieclxx.exe");
  for(int k=0;!pid&&k<12;k++){Sleep(200);pid=FindPID(L"atieclxx.exe");}
  if(pid)success=Measure(pid,5000,pRate,dRate,dMeasured);
 }while(false);
 bool restored=true;
 if(started)restored=StopAMD()&&restored;
 if(changed)restored=SetStart(mode)&&restored;
 return success&&restored;
}
static std::wstring TempResultPath(){
 wchar_t dir[MAX_PATH]{};GetTempPathW(MAX_PATH,dir);
 wchar_t file[MAX_PATH]{};GetTempFileNameW(dir,L"ATF",0,file);
 return file;
}
static bool WriteResult(const std::wstring& path,bool ok,double p,double d,bool dm){
 HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return false;
 char buf[256]{};sprintf_s(buf,"%d %.9f %.9f %d\n",ok?1:0,p,d,dm?1:0);
 DWORD wr=0;bool result=WriteFile(h,buf,(DWORD)strlen(buf),&wr,nullptr)!=0;CloseHandle(h);return result;
}
static bool ReadResult(const std::wstring& path,double& p,double& d,bool& dm){
 FILE* f=nullptr;
 if(_wfopen_s(&f,path.c_str(),L"rb")||!f)return false;
 int ok=0,flag=0;int rc=fscanf_s(f,"%d %lf %lf %d",&ok,&p,&d,&flag);
 fclose(f);dm=flag!=0;return rc==4&&ok==1;
}
struct WorkerResult{State state;bool temporary=false,success=false;};
static DWORD WINAPI RefreshWorker(LPVOID context){
 std::unique_ptr<WorkerResult> data((WorkerResult*)context);
 if(data->temporary){
  if(IsAdmin()){data->success=TemporaryTest(data->state.processMBps,data->state.diskMBps,data->state.diskMeasured);}
  else{
   std::wstring output=TempResultPath();
   std::wstring self=GetSelfPath();
   SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);
   info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;info.lpVerb=L"runas";info.lpFile=self.c_str();
   std::wstring params=L"--temporary-test \""+output+L"\"";
   info.lpParameters=params.c_str();info.nShow=SW_HIDE;
   if(ShellExecuteExW(&info) && info.hProcess){
    DWORD wait=WaitForSingleObject(info.hProcess,45000);
    if(wait==WAIT_OBJECT_0){
     DWORD exitCode=1;GetExitCodeProcess(info.hProcess,&exitCode);
     if(exitCode==0)data->success=ReadResult(output,data->state.processMBps,data->state.diskMBps,data->state.diskMeasured);
    }else if(wait==WAIT_TIMEOUT)TerminateProcess(info.hProcess,1); // Service restoration is in child.
    CloseHandle(info.hProcess);
   }
   DeleteFileW(output.c_str());
  }
  State current=Inspect(false);current.processMeasured=data->success;
  current.historical=data->success;current.processMBps=data->state.processMBps;
  current.diskMBps=data->state.diskMBps;current.diskMeasured=data->state.diskMeasured;
  current.message=data->success?L"Taxas somente do teste SEM proteção. Serviço AMD restaurado.":L"Teste cancelado ou sem medição válida; confira o serviço AMD.";
  data->state=current;
 }else data->state=Inspect(true);
 PostMessageW(gWnd,WM_DIAGNOSTIC,0,(LPARAM)data.release());
 return 0;
}
static void StartWorker(bool temp){
 if(gWorking)return;
 gWorking=true;
 EnableWindow(gRefresh,FALSE);EnableWindow(gTest,FALSE);EnableWindow(gEnable,FALSE);EnableWindow(gDisable,FALSE);
 auto* data=new WorkerResult();data->temporary=temp;
 HANDLE th=CreateThread(nullptr,0,RefreshWorker,data,0,nullptr);
 if(th)CloseHandle(th);else{delete data;gWorking=false;}
 InvalidateRect(gWnd,nullptr,TRUE);
}
static int RunElevated(const std::wstring& cmd){
 if(IsAdmin())return cmd==L"--enable"?(EnableProtection()?0:1):cmd==L"--disable"?(DisableProtection()?0:1):1;
 std::wstring path=GetSelfPath();SHELLEXECUTEINFOW s{};s.cbSize=sizeof(s);s.fMask=SEE_MASK_NOCLOSEPROCESS;
 s.lpVerb=L"runas";s.lpFile=path.c_str();s.lpParameters=cmd.c_str();s.nShow=SW_HIDE;
 if(!ShellExecuteExW(&s))return 2;
 DWORD ret=WaitForSingleObject(s.hProcess,60000);
 DWORD exitCode=1;if(ret==WAIT_OBJECT_0)GetExitCodeProcess(s.hProcess,&exitCode);
 CloseHandle(s.hProcess);return exitCode==0?0:1;
}
static void PaintLabel(HDC dc,RECT rc,const std::wstring& text,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER){
 SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);
 DrawTextW(dc,text.c_str(),(int)text.size(),&rc,flags);
}
static void FillRounded(HDC dc,RECT r,COLORREF color,int radius=12){
 HBRUSH b=CreateSolidBrush(color);HPEN p=CreatePen(PS_SOLID,1,color);
 HGDIOBJ ob=SelectObject(dc,b),op=SelectObject(dc,p);
 RoundRect(dc,r.left,r.top,r.right,r.bottom,radius*2,radius*2);
 SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(b);DeleteObject(p);
}
static void PaintWindow(HDC dc,RECT client){
 HBRUSH b=CreateSolidBrush(BG);FillRect(dc,&client,b);DeleteObject(b);
 RECT top{0,0,client.right,70};FillRounded(dc,top,RGB(18,27,36),0);
 PaintLabel(dc,{30,10,client.right-20,31},L"RADEON  /  ATIECLXX FIX",gFontTitle,WHITE);
 PaintLabel(dc,{31,36,client.right-20,55},L"PROTEÇÃO DO SSD  ·  VERSÃO EXPERIMENTAL",gFontSmall,ACCENT);
 PaintLabel(dc,{30,81,client.right-30,116},L"Diagnóstico de gravações no SSD",gFontTitle,WHITE);
 PaintLabel(dc,{30,117,client.right-30,146},L"Identifica gravações repetitivas e mantém o driver original intacto.",gFont,MUTED);
 RECT hero{28,158,client.right-28,256};FillRounded(dc,hero,RGB(25,41,48),14);
 std::wstring title,detail;COLORREF tone=ACCENT;
 if(gWorking){title=L"Verificando...";detail=L"Coletando dados do Windows. Aguarde a conclusão.";tone=WARN;}
 else if(!gState.validAMD){title=L"Driver AMD não verificado";detail=gState.message;tone=WARN;}
 else if(gState.historical){
  title=gState.processMeasured&&gState.processMBps>=0.15?L"Problema original confirmado":L"Teste concluído sem escrita excessiva";
  detail=gState.message;tone=gState.processMBps>=0.15?RED:ACCENT;
 }else if(!gState.serviceKnown){
  title=L"Estado do serviço indisponível";detail=gState.message;tone=WARN;
 }else if(!gState.serviceRunning){
  title=gState.installed||gState.legacy?L"Proteção indisponível — serviço parado":L"Serviço AMD parado";
  detail=gState.message;tone=WARN;
 }else if(!gState.pid){
  title=L"Serviço em execução; cliente não detectado";detail=gState.message;tone=WARN;
 }else if(gState.legacy||gState.installed){
  title=gState.processMeasured&&gState.processMBps<0.15?L"Proteção ativa — serviço funcionando":L"Proteção registrada — verificar gravações";
  detail=gState.message;tone=gState.processMeasured&&gState.processMBps<0.15?ACCENT:WARN;
 }else if(gState.processMeasured&&gState.processMBps>=0.15){
  title=L"Escrita excessiva detectada";detail=gState.message;tone=RED;
 }else {title=L"Sem escrita excessiva na amostra";detail=gState.message;tone=ACCENT;}
 PaintLabel(dc,{48,174,client.right-52,207},title,gFontTitle,tone);
 PaintLabel(dc,{48,213,client.right-52,243},detail,gFont,MUTED);
 int gap=12;int left=28;int total=client.right-56;int cw=(total-gap*3)/4;
 auto metric=[&](int index,const std::wstring& lab,const std::wstring& val,const std::wstring& note,COLORREF valColor){
  int x=left+index*(cw+gap);RECT r{x,273,x+cw,380};FillRounded(dc,r,PANEL);
  PaintLabel(dc,{x+13,282,x+cw-9,303},lab,gFontSmall,MUTED);
  PaintLabel(dc,{x+13,307,x+cw-9,344},val,gFontTitle,valColor);
  PaintLabel(dc,{x+13,348,x+cw-9,373},note,gFontSmall,MUTED);
 };
 wchar_t val[50]{},disk[50]{};
 if(gState.processMeasured)swprintf_s(val,L"%.3f MB/s",gState.processMBps);
 else wcscpy_s(val,L"—");
 if(gState.diskMeasured)swprintf_s(disk,L"%.3f MB/s",gState.diskMBps);
 else wcscpy_s(disk,L"—");
 metric(0,L"DRIVER AMD",gState.supported?L"Compatível":gState.validAMD?L"Outra versão":L"—",gState.version.empty()?L"Identidade":gState.version,gState.supported?ACCENT:WARN);
 metric(1,L"PROCESSO AMD",val,gState.historical?L"Teste SEM proteção (5 s)":L"Gravações do processo",gState.processMeasured&&gState.processMBps>=0.15?RED:WHITE);
 metric(2,L"DISCO C: TOTAL",disk,L"Todos os processos",WHITE);
 metric(3,L"SERVIÇO AMD",!gState.serviceKnown?L"Indisponível":gState.serviceRunning?L"Em execução":L"Parado",gState.serviceMode,gState.serviceRunning?ACCENT:WARN);
 RECT r{28,398,client.right-28,475};FillRounded(dc,r,RGB(18,27,36),12);
 PaintLabel(dc,{44,405,client.right-40,431},L"Como funciona",gFont,WHITE);
 PaintLabel(dc,{44,433,client.right-43,468},L"Verifica as gravações do processo AMD. A correção só é ativada em versões compatíveis.",gFontSmall,MUTED);
 if(gState.hash.size()>0){
  std::wstring shortened=gState.hash.substr(0,22)+L"...";
  PaintLabel(dc,{30,486,client.right-30,510},L"SHA-256 do cliente: "+shortened,gFontSmall,MUTED);
 }
 if(gWorking)PaintLabel(dc,{30,516,client.right-28,540},L"Leitura em andamento...",gFontSmall,WARN);
 else PaintLabel(dc,{30,516,client.right-28,540},L"Nenhum arquivo original do driver é substituído. Gravações totais incluem outros programas.",gFontSmall,MUTED);
}
static void SetControls(){
 EnableWindow(gRefresh,!gWorking);
 bool canTest=!gWorking&&gState.validAMD&&gState.serviceKnown&&!gState.serviceRunning&&!gState.pid&&!gState.legacy&&!gState.installed;
 EnableWindow(gTest,canTest);
 ShowWindow(gTest,canTest?SW_SHOW:SW_HIDE);
 bool canEnable=!gWorking&&gState.supported&&!gState.installed&&!gState.legacy
   &&gState.processMeasured&&gState.processMBps>=0.15;
 EnableWindow(gEnable,canEnable);
 ShowWindow(gEnable,gState.installed||gState.legacy?SW_HIDE:SW_SHOW);
 bool canDisable=!gWorking&&(gState.installed);
 EnableWindow(gDisable,canDisable);
 ShowWindow(gDisable,gState.installed?SW_SHOW:SW_HIDE);
}
static void DrawButton(LPDRAWITEMSTRUCT it){
 bool enabled=IsWindowEnabled(it->hwndItem)!=FALSE;
 bool primary=it->CtlID==ID_ENABLE;
 COLORREF color=enabled?(primary?ACCENT:RGB(38,51,63)):RGB(28,37,46);
 FillRounded(it->hDC,it->rcItem,color,10);
 wchar_t text[128]{};GetWindowTextW(it->hwndItem,text,128);
 RECT r=it->rcItem;
 PaintLabel(it->hDC,r,text,gFont,enabled?(primary?BG:WHITE):RGB(104,117,129),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
static LRESULT CALLBACK WndProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_CREATE:{
  gFont=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  gFontTitle=CreateFontW(-23,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  gFontSmall=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  auto btn=[&](int id,const wchar_t* text,int width){
   HWND b=CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,width,43,w,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
   SendMessageW(b,WM_SETFONT,(WPARAM)gFont,TRUE);return b;
  };
  gRefresh=btn(ID_REFRESH,L"Verificar novamente",178);
  gTest=btn(ID_TEST,L"Testar sem proteção",185);
  gEnable=btn(ID_ENABLE,L"Ativar proteção",166);
  gDisable=btn(ID_DISABLE,L"Remover proteção",178);
  StartWorker(false);return 0;
 }
 case WM_SIZE:{
  int width=LOWORD(lp),height=HIWORD(lp);
  int x=width-29;
  MoveWindow(gEnable,x-166,height-72,166,43,TRUE);
  MoveWindow(gDisable,x-178,height-72,178,43,TRUE);
  MoveWindow(gRefresh,x-178,91,178,43,TRUE);
  MoveWindow(gTest,x-178-198,91,185,43,TRUE);
  InvalidateRect(w,nullptr,TRUE);return 0;
 }
 case WM_DRAWITEM: DrawButton((LPDRAWITEMSTRUCT)lp);return TRUE;
 case WM_COMMAND:
  if(HIWORD(wp)==BN_CLICKED){
   if(LOWORD(wp)==ID_REFRESH)StartWorker(false);
   else if(LOWORD(wp)==ID_TEST){
    if(MessageBoxW(w,L"Este teste reproduz o funcionamento ORIGINAL, SEM proteção. Ele pode causar gravações no SSD durante cerca de cinco segundos.\n\nNão significa que a correção continua gravando. Depois, o serviço AMD voltará ao estado anterior.\n\nDeseja realizar o teste?",L"Teste temporário sem proteção",MB_YESNO|MB_ICONQUESTION)==IDYES)StartWorker(true);
   }else if(LOWORD(wp)==ID_ENABLE){
    if(MessageBoxW(w,L"Esta correção experimental cria um monitor nativo como tarefa SYSTEM. O executável assinado da AMD não será alterado.\n\nAtivar proteção?",L"Confirmação",MB_YESNO|MB_ICONWARNING)==IDYES){
     int result=RunElevated(L"--enable");
     if(result!=0)MessageBoxW(w,L"A proteção não foi ativada. O serviço AMD pode permanecer parado para evitar gravações. Consulte o estado mostrado após a verificação.\n\nNenhum sucesso foi confirmado.",L"Ativação não concluída",MB_OK|MB_ICONWARNING);
     StartWorker(false);
    }
   }else if(LOWORD(wp)==ID_DISABLE){
    if(MessageBoxW(w,L"Remover o monitor? O serviço AMD ficará desativado para evitar a volta das gravações.",L"Confirmar remoção",MB_YESNO|MB_ICONQUESTION)==IDYES){
     int result=RunElevated(L"--disable");
     if(result!=0)MessageBoxW(w,L"A remoção não foi confirmada. Confira o estado do serviço e da proteção após a verificação.",L"Remoção não concluída",MB_OK|MB_ICONWARNING);
     StartWorker(false);
    }
   }
  }return 0;
 case WM_DIAGNOSTIC:{
  std::unique_ptr<WorkerResult> r((WorkerResult*)lp);
  gState=r->state;gWorking=false;SetControls();InvalidateRect(w,nullptr,TRUE);return 0;
 }
 case WM_PAINT:{
  PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT client{};GetClientRect(w,&client);
  HDC mem=CreateCompatibleDC(dc);HBITMAP bm=CreateCompatibleBitmap(dc,client.right,client.bottom);
  HGDIOBJ prev=SelectObject(mem,bm);PaintWindow(mem,client);BitBlt(dc,0,0,client.right,client.bottom,mem,0,0,SRCCOPY);
  SelectObject(mem,prev);DeleteObject(bm);DeleteDC(mem);EndPaint(w,&ps);return 0;
 }
 case WM_CTLCOLORBTN: return (LRESULT)GetStockObject(NULL_BRUSH);
 case WM_DESTROY:
  DeleteObject(gFont);DeleteObject(gFontTitle);DeleteObject(gFontSmall);PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(w,msg,wp,lp);
}
static int GUI(HINSTANCE h,int show){
 SetProcessDPIAware();
 WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=WndProc;wc.hInstance=h;
 wc.lpszClassName=L"AMDAtieclxxNativeLite";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
 wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
 RegisterClassExW(&wc);
 gWnd=CreateWindowExW(0,wc.lpszClassName,L"AMD atieclxx Fix — Nativo / Portátil",WS_OVERLAPPEDWINDOW,
   CW_USEDEFAULT,CW_USEDEFAULT,950,620,nullptr,nullptr,h,nullptr);
 if(!gWnd)return 1;
 ShowWindow(gWnd,show);UpdateWindow(gWnd);
 MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
 return (int)msg.wParam;
}
int APIENTRY wWinMain(HINSTANCE h,HINSTANCE,LPWSTR args,int show){
 std::wstring arg=Trim(args);
 if(arg==L"--watch")return Watch()?0:1;
 if(arg==L"--enable")return EnableProtection()?0:1;
 if(arg==L"--disable")return DisableProtection()?0:1;
 if(arg==L"--once")return Apply()?0:1;
 if(arg.rfind(L"--temporary-test ",0)==0){
  double p=0,d=0;bool dm=false;bool ok=TemporaryTest(p,d,dm);
  std::wstring path=Trim(arg.substr(wcslen(L"--temporary-test ")));
  WriteResult(path,ok,p,d,dm);return ok?0:1;
 }
 if(arg==L"--check"){
  State st=Inspect(true);wchar_t b[512]{};swprintf_s(b,L"vendor=%d supported=%d pid=%lu processMeasured=%d processRate=%.6fMBps diskMeasured=%d diskRate=%.6fMBps",st.validAMD,st.supported,st.pid,st.processMeasured,st.processMBps,st.diskMeasured,st.diskMBps);
  MessageBoxW(nullptr,b,L"Native diagnostic",MB_OK);return 0;
 }
 return GUI(h,show);
}
