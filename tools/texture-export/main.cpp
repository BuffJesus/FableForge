#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <future>
#include <string>
#include "textureproject.hpp"
#include "collection.hpp"
#include "import.hpp"
#include "forge/env.hpp"
#include <nlohmann/json.hpp>

namespace fs=std::filesystem;
namespace tp=albion::textureproject;
// Export control IDs are stable: tools/test_texture_export_gui.py drives them.
constexpr int Game=101,Output=102,GameBrowse=103,OutputBrowse=104,Start=105,Secondary=106,Status=107,Compression=108;
constexpr int TabExport=201,TabImport=202,Textures=203,TexturesBrowse=204,ImportStart=205;
constexpr const wchar_t* compressionLabels[]={L"Fast",L"Balanced",L"Smallest files"};
constexpr int compressionLevels[]={1,3,9};
constexpr const wchar_t* compressionHints[]={L"Larger files",L"Recommended",L"Longer export"};
int selectedCompression=1;
constexpr COLORREF Bg=RGB(19,18,26),Panel=RGB(32,30,42),Text=RGB(232,229,242),Muted=RGB(160,155,177),Accent=RGB(141,87,246);
// Layout rows (logical pixels at 96 DPI).
constexpr int TabsY=92,GameY=174,SecondY=254,ProgressY=436,StatusY=456,ButtonsY=540,Height=604;
enum class Page {Export,Import};
enum class ImportStage {Idle,Reviewing,Reviewed,Importing,Done};
HWND window,gameInput,outputInput,texturesInput,startButton,secondary,importButton,statusLabel;
HFONT body,title,small;HBRUSH background,field;float scale=1;bool failed=false,completed=false,closing=false;
Page page=Page::Export;ImportStage importStage=ImportStage::Idle;
std::future<tp::Result> job;tp::Progress progress;fs::path exportFolder,runtimeFolder;
std::future<textureimport::Plan> reviewJob;std::future<textureimport::Outcome> importJob;textureimport::Plan plan;
std::wstring exportStatus=L"Organized PNG images and original texture data. Your game files stay unchanged.";
std::wstring importStatus=L"Edit PNGs in your exported “Fable textures” folder (keep names and folders), then review them here.";
bool exportFailed=false,importFailed=false;
int px(int value){return int(value*scale);}
std::wstring widen(const std::string& value) {if(value.empty()) return {};int count=MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),nullptr,0);std::wstring text(count,0);MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),text.data(),count);return text;}
std::wstring textOf(HWND control){int count=GetWindowTextLengthW(control);std::wstring value(count+1,0);GetWindowTextW(control,value.data(),count+1);value.resize(count);return value;}
bool importBusy() {return importStage==ImportStage::Reviewing || importStage==ImportStage::Importing;}
bool busy() {return job.valid() || importBusy();}
void showStatus() {
    const auto& text=page==Page::Export?exportStatus:importStatus;const bool error=page==Page::Export?exportFailed:importFailed;
    const bool colorChanged=failed!=error;failed=error;
    if(textOf(statusLabel)!=text) SetWindowTextW(statusLabel,text.c_str());
    else if(colorChanged) InvalidateRect(statusLabel,nullptr,FALSE);
    RECT bar{px(28),px(ProgressY),px(632),px(ProgressY+6)};InvalidateRect(window,&bar,FALSE);
}
// Each page keeps its own message; only the visible page's message is shown.
void status(const std::wstring& text,bool error=false){exportStatus=text;exportFailed=error;if(page==Page::Export) showStatus();}
void importMessage(const std::wstring& text,bool error=false){importStatus=text;importFailed=error;if(page==Page::Import) showStatus();}
fs::path chooseFolder(const wchar_t* titleText,const fs::path& initial) {
    IFileOpenDialog* dialog=nullptr;fs::path result;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) return result;
    dialog->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);dialog->SetTitle(titleText);
    IShellItem* folder=nullptr;if(!initial.empty() && SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_PPV_ARGS(&folder)))) {dialog->SetFolder(folder);folder->Release();}
    if(SUCCEEDED(dialog->Show(window))) {IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))) {PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {result=path;CoTaskMemFree(path);}item->Release();}}
    dialog->Release();return result;
}
void layout() {
    const bool exporting=page==Page::Export;
    for(int id:{Output,OutputBrowse,Start,Compression,Compression+1,Compression+2}) ShowWindow(GetDlgItem(window,id),exporting?SW_SHOW:SW_HIDE);
    ShowWindow(secondary,exporting && (job.valid() || completed)?SW_SHOW:SW_HIDE);
    for(int id:{Textures,TexturesBrowse,ImportStart}) ShowWindow(GetDlgItem(window,id),exporting?SW_HIDE:SW_SHOW);
    const bool idle=!busy();
    for(int id:{Game,GameBrowse,Output,OutputBrowse,Start,Compression,Compression+1,Compression+2,Textures,TexturesBrowse,ImportStart,TabExport,TabImport})
        EnableWindow(GetDlgItem(window,id),idle);
    EnableWindow(secondary,exporting && (job.valid() || completed));
    InvalidateRect(window,nullptr,FALSE);showStatus();
}
void beginExport() {
    if(busy()) return;
    completed=false;SetWindowTextW(startButton,L"Export textures");SetWindowTextW(secondary,L"Cancel");
    try {
        const fs::path root=textOf(gameInput),parent=textOf(outputInput);
        if(root.empty() || !fs::is_regular_file(root/"data/graphics/pc/textures.big")) {status(L"Choose the Fable game folder containing data\\graphics\\pc\\textures.big.",true);return;}
        if(parent.empty() || !fs::is_directory(parent)) {status(L"Choose an existing folder for your exported textures.",true);return;}
        exportFolder=parent/L"Fable textures";for(unsigned n=2;fs::exists(exportFolder);++n) exportFolder=parent/(L"Fable textures ("+std::to_wstring(n)+L")");
        failed=false;progress.done=0;progress.total=0;progress.cancel=false;
        SetWindowTextW(startButton,L"Exporting textures...");
        status(L"Checking the texture archive. Your game files stay unchanged.");
        const auto destination=exportFolder;
        const int compression=compressionLevels[selectedCompression];
        job=std::async(std::launch::async,[root,destination,compression] {return texturecollection::exportInstallation(root,destination,&progress,compression);});
        layout();
    } catch(const std::exception& e) {status(widen(e.what()),true);}
}
std::wstring summary(const textureimport::Plan& p) {
    std::wstring text=std::to_wstring(p.changed)+L" edited texture"+(p.changed==1?L"":L"s")+L" will be imported. "+std::to_wstring(p.unchanged)+L" unchanged";
    if(p.missing) text+=L", "+std::to_wstring(p.missing)+L" deleted (they keep the original)";
    if(p.restoredArchives)text+=L".\nOriginal textures will be restored in "+std::to_wstring(p.restoredArchives)+L" archive"+(p.restoredArchives==1?L"":L"s");
    if(p.needsArchive4GB)text+=L".\nThis bank exceeds 2 GiB. The verified Archive4GB DLL will be installed and enabled with it (audited Steam TLC executable required)";
    return text+L".\nYour original archives are kept once as .forge-orig backups.";
}
void beginReview() {
    const fs::path root=textOf(gameInput),textures=textOf(texturesInput);
    if(root.empty() || !fs::is_regular_file(root/"data/graphics/pc/textures.big")) {importMessage(L"Choose the Fable game folder containing data\\graphics\\pc\\textures.big.",true);return;}
    if(textures.empty() || !fs::is_regular_file(textures/"texture-project.json")) {importMessage(L"Choose your “Fable textures” folder (the one holding texture-project.json).",true);return;}
    progress.done=0;progress.total=0;progress.cancel=false;importStage=ImportStage::Reviewing;
    SetWindowTextW(importButton,L"Reviewing...");importMessage(L"Comparing your images with the game's textures. Nothing is changed yet.");
    reviewJob=std::async(std::launch::async,[root,textures]{return textureimport::review(root,textures,&progress);});
    layout();
}
void beginImport() {
    const fs::path root=textOf(gameInput);
    progress.done=0;progress.total=0;progress.cancel=false;importStage=ImportStage::Importing;
    SetWindowTextW(importButton,L"Importing...");importMessage(L"Writing the new texture archives. Keep Fable closed.");
    importJob=std::async(std::launch::async,[root]{return textureimport::apply(root,plan,runtimeFolder,&progress);});
    layout();
}
void resetImport() {if(!importBusy() && importStage!=ImportStage::Idle) {importStage=ImportStage::Idle;SetWindowTextW(importButton,L"Review changes");}}
HWND control(const wchar_t* cls,const wchar_t* caption,int id,int x,int y,int w,int h,DWORD style=0) {
    HWND child=CreateWindowExW(0,cls,caption,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);return child;
}
void drawText(HDC dc,const wchar_t* text,int x,int y,int w,int h,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_WORDBREAK) {
    SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);RECT r{px(x),px(y),px(x+w),px(y+h)};DrawTextW(dc,text,-1,&r,flags);
}
void finishImportJobs(HWND hwnd) {
    if(importStage==ImportStage::Reviewing && reviewJob.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
        plan=reviewJob.get();
        if(!plan.error.empty()) {importStage=ImportStage::Idle;SetWindowTextW(importButton,L"Review changes");importMessage(widen(plan.error),true);}
        else if(!plan.hasChanges()) {importStage=ImportStage::Idle;SetWindowTextW(importButton,L"Review again");importMessage(L"No edited textures found. Save your changes over the exported PNGs (same names and folders), then review again.");}
        else {importStage=ImportStage::Reviewed;SetWindowTextW(importButton,L"Import into game");importMessage(summary(plan));}
        layout();if(closing) DestroyWindow(hwnd);
    } else if(importStage==ImportStage::Importing && importJob.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
        const auto outcome=importJob.get();
        if(outcome.ok) {
            importStage=ImportStage::Done;SetWindowTextW(importButton,L"Review again");
            std::wstring text=std::to_wstring(outcome.imported)+L" textures imported. "+std::to_wstring(outcome.resized)+L" larger textures keep their normal on-screen size (TextureSizeFix).";
            if(outcome.restoredArchives)text+=L"\nOriginal textures restored in "+std::to_wstring(outcome.restoredArchives)+L" archive"+(outcome.restoredArchives==1?L".":L"s.");
            if(outcome.archive4GBInstalled)text+=L"\nArchive4GB installed and enabled for this bank. This changes the archive limit; RAM limits are separate.";
            for(const auto& note:outcome.notes) if(note.rfind("No Mods.ini",0)==0 || note.rfind("Some imported",0)==0) text+=L"\n"+widen(note);
            importMessage(text+L"\nStart Fable through your usual mod launcher.");
        } else {importStage=ImportStage::Idle;SetWindowTextW(importButton,L"Review again");importMessage(widen(outcome.error),true);}
        layout();if(closing) DestroyWindow(hwnd);
    } else if(importBusy()) {
        const auto total=progress.total.load(),done=progress.done.load();
        if(total) importMessage(std::wstring(importStage==ImportStage::Reviewing?L"Reviewing: ":L"Importing: ")+std::to_wstring(std::min(done,total))+L" of "+std::to_wstring(total));
    }
}
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE: {
        window=hwnd;body=CreateFontW(-px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        title=CreateFontW(-px(25),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        small=CreateFontW(-px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        control(L"BUTTON",L"Export",TabExport,28,TabsY,120,36,WS_TABSTOP|BS_OWNERDRAW);
        control(L"BUTTON",L"Import",TabImport,152,TabsY,120,36,WS_TABSTOP|BS_OWNERDRAW);
        gameInput=control(L"EDIT",L"",Game,38,GameY+7,468,22,WS_TABSTOP|ES_AUTOHSCROLL);
        outputInput=control(L"EDIT",L"",Output,38,SecondY+7,468,22,WS_TABSTOP|ES_AUTOHSCROLL);
        texturesInput=control(L"EDIT",L"",Textures,38,SecondY+7,468,22,WS_TABSTOP|ES_AUTOHSCROLL);
        control(L"BUTTON",L"Browse...",GameBrowse,528,GameY,104,36,WS_TABSTOP|BS_OWNERDRAW);
        control(L"BUTTON",L"Browse...",OutputBrowse,528,SecondY,104,36,WS_TABSTOP|BS_OWNERDRAW);
        control(L"BUTTON",L"Browse...",TexturesBrowse,528,SecondY,104,36,WS_TABSTOP|BS_OWNERDRAW);
        for(int i=0;i<3;++i)control(L"BUTTON",compressionLabels[i],Compression+i,28+i*204,360,196,52,WS_TABSTOP|BS_OWNERDRAW);
        statusLabel=control(L"STATIC",exportStatus.c_str(),Status,28,StatusY,604,70);
        startButton=control(L"BUTTON",L"Export textures",Start,412,ButtonsY,220,44,WS_TABSTOP|BS_OWNERDRAW);
        importButton=control(L"BUTTON",L"Review changes",ImportStart,412,ButtonsY,220,44,WS_TABSTOP|BS_OWNERDRAW);
        secondary=control(L"BUTTON",L"Cancel",Secondary,280,ButtonsY,120,44,WS_TABSTOP|BS_OWNERDRAW);
        BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof dark);COLORREF caption=Bg;DwmSetWindowAttribute(hwnd,35,&caption,sizeof caption);SetTimer(hwnd,1,100,nullptr);
        layout();return 0;
    }
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT: {
        HDC dc=reinterpret_cast<HDC>(wp);const auto child=reinterpret_cast<HWND>(lp);const bool input=child==gameInput || child==outputInput || child==texturesInput;
        SetTextColor(dc,input?(IsWindowEnabled(child)?Text:Muted):(failed?RGB(247,150,160):Muted));SetBkColor(dc,input?Panel:Bg);return reinterpret_cast<LRESULT>(input?field:background);
    }
    case WM_DRAWITEM: {
        auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);const bool enabled=!(item.itemState&ODS_DISABLED);
        if(item.CtlID==TabExport || item.CtlID==TabImport) {
            const bool selected=(item.CtlID==TabExport)==(page==Page::Export);
            FillRect(item.hDC,&item.rcItem,background);
            SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,selected?Text:Muted);SelectObject(item.hDC,body);
            auto text=textOf(item.hwndItem);RECT r=item.rcItem;r.bottom-=px(4);DrawTextW(item.hDC,text.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if(selected) {RECT line{item.rcItem.left+px(16),item.rcItem.bottom-px(3),item.rcItem.right-px(16),item.rcItem.bottom};HBRUSH b=CreateSolidBrush(Accent);FillRect(item.hDC,&line,b);DeleteObject(b);}
            if(item.itemState&ODS_FOCUS) {RECT f=item.rcItem;InflateRect(&f,-3,-3);DrawFocusRect(item.hDC,&f);}
            return TRUE;
        }
        if(item.CtlID>=Compression && item.CtlID<Compression+3) {
            const int index=item.CtlID-Compression;const bool selected=index==selectedCompression;
            HBRUSH brush=CreateSolidBrush(selected?RGB(44,34,65):Panel);FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);
            brush=CreateSolidBrush(selected?Accent:RGB(65,60,78));FrameRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);
            SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,enabled?Text:Muted);SelectObject(item.hDC,body);
            RECT r=item.rcItem;r.left+=px(12);r.top+=px(7);r.bottom=r.top+px(20);DrawTextW(item.hDC,compressionLabels[index],-1,&r,DT_LEFT|DT_SINGLELINE);
            r.top+=px(21);r.bottom=item.rcItem.bottom;SelectObject(item.hDC,small);SetTextColor(item.hDC,Muted);DrawTextW(item.hDC,compressionHints[index],-1,&r,DT_LEFT|DT_SINGLELINE);
            if(selected){RECT mark{item.rcItem.right-px(18),item.rcItem.top+px(12),item.rcItem.right-px(12),item.rcItem.top+px(18)};brush=CreateSolidBrush(Accent);FillRect(item.hDC,&mark,brush);DeleteObject(brush);}
            if(item.itemState&ODS_FOCUS){RECT focus=item.rcItem;InflateRect(&focus,-3,-3);DrawFocusRect(item.hDC,&focus);}return TRUE;
        }
        const bool primary=item.CtlID==Start || item.CtlID==ImportStart;
        HBRUSH brush=CreateSolidBrush(primary&&enabled?Accent:Panel);FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);
        SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,enabled?Text:Muted);SelectObject(item.hDC,body);
        auto text=textOf(item.hwndItem);DrawTextW(item.hDC,text.c_str(),-1,&item.rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(item.itemState&ODS_FOCUS) {RECT r=item.rcItem;InflateRect(&r,-4,-4);DrawFocusRect(item.hDC,&r);}return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;HDC dc=BeginPaint(hwnd,&paint);RECT client;GetClientRect(hwnd,&client);FillRect(dc,&client,background);
        const bool exporting=page==Page::Export;
        drawText(dc,L"Fable Textures",28,20,604,36,title,Text);
        drawText(dc,L"Export, edit and re-import the textures of your own copy of Fable.",28,58,604,24,body,Muted);
        RECT rule{px(28),px(TabsY+36),px(632),px(TabsY+37)};HBRUSH line=CreateSolidBrush(RGB(48,45,60));FillRect(dc,&rule,line);DeleteObject(line);
        drawText(dc,L"Game folder",28,GameY-24,604,22,body,Text);
        drawText(dc,exporting?L"Export location":L"Exported “Fable textures” folder",28,SecondY-24,604,22,body,Text);
        HBRUSH border=CreateSolidBrush(RGB(65,60,78));
        for(int y:{GameY,SecondY}) {RECT box{px(28),px(y),px(516),px(y+36)};FillRect(dc,&box,field);FrameRect(dc,&box,border);}DeleteObject(border);
        if(exporting) {
            drawText(dc,L"A new “Fable textures” folder will be created here.",28,SecondY+44,604,24,small,Muted);
            drawText(dc,L"PNG compression",28,334,220,22,body,Text);
            drawText(dc,L"All settings are lossless",364,336,268,22,small,Muted,DT_RIGHT|DT_SINGLELINE);
        } else {
            drawText(dc,L"Edited images can be larger. Menus and the HUD keep their normal size through TextureSizeFix, which is installed with your textures.",28,SecondY+44,604,40,small,Muted);
        }
        RECT track{px(28),px(ProgressY),px(632),px(ProgressY+6)};
        const bool finished=exporting?completed:importStage==ImportStage::Done;
        const bool running=exporting?job.valid():importBusy();
        if(running || finished) FillRect(dc,&track,field);
        const auto total=progress.total.load(),done=progress.done.load();
        if((running && total) || finished) {track.right=track.left+int(px(604)*(finished?1.0:std::min(1.0,double(done)/double(total?total:1))));HBRUSH b=CreateSolidBrush(Accent);FillRect(dc,&track,b);DeleteObject(b);}
        EndPaint(hwnd,&paint);return 0;
    }
    case WM_ERASEBKGND:return 1;
    case WM_LBUTTONDOWN: {
        const int x=LOWORD(lp),y=HIWORD(lp);
        if(x>=px(28) && x<=px(516)) {
            if(y>=px(GameY) && y<=px(GameY+36))SetFocus(gameInput);
            if(y>=px(SecondY) && y<=px(SecondY+36))SetFocus(page==Page::Export?outputInput:texturesInput);
        }break;
    }
    case WM_COMMAND:
        if(HIWORD(wp)==EN_CHANGE && (LOWORD(wp)==Game || LOWORD(wp)==Textures)) {resetImport();return 0;}
        if(HIWORD(wp)!=BN_CLICKED) break;
        if(LOWORD(wp)>=Compression && LOWORD(wp)<Compression+3) {
            selectedCompression=LOWORD(wp)-Compression;
            for(int i=0;i<3;++i)InvalidateRect(GetDlgItem(hwnd,Compression+i),nullptr,FALSE);
            return 0;
        }
        switch(LOWORD(wp)) {
        case TabExport:case TabImport:
            if(!busy()) {page=LOWORD(wp)==TabExport?Page::Export:Page::Import;
                InvalidateRect(GetDlgItem(hwnd,TabExport),nullptr,FALSE);InvalidateRect(GetDlgItem(hwnd,TabImport),nullptr,FALSE);layout();}
            return 0;
        case GameBrowse:case OutputBrowse:case TexturesBrowse: {
            HWND input=LOWORD(wp)==GameBrowse?gameInput:LOWORD(wp)==OutputBrowse?outputInput:texturesInput;
            const wchar_t* prompt=input==gameInput?L"Choose your Fable game folder":input==outputInput?L"Choose where to save exported textures":L"Choose your exported “Fable textures” folder";
            auto path=chooseFolder(prompt,textOf(input));if(!path.empty()) SetWindowTextW(input,path.c_str());return 0;
        }
        case Start:if(completed) ShellExecuteW(hwnd,L"open",exportFolder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);else beginExport();return 0;
        case Secondary:if(job.valid()) {progress.cancel=true;status(L"Cancelling. Temporary exports will be cleaned up.");}else {completed=false;beginExport();}return 0;
        case ImportStart:if(importStage==ImportStage::Reviewed && plan.hasChanges()) beginImport();else beginReview();return 0;
        }break;
    case WM_TIMER:
        if(job.valid()) {
            if(job.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
                tp::Result result;try{result=job.get();}catch(const std::exception& e){result.error=e.what();}
                if(result.ok) {completed=true;SetWindowTextW(startButton,L"Open exported textures");SetWindowTextW(secondary,L"Export again");
                    // Point the Import page at what was just exported.
                    SetWindowTextW(texturesInput,exportFolder.c_str());
                    status(std::to_wstring(result.exported)+L" PNG images exported. All "+std::to_wstring(result.originals)+L" source items preserved.\n"+std::to_wstring(result.skipped)+L" items need attention. See export-report.json for coverage and details.");}
                else {SetWindowTextW(startButton,L"Export textures");status(widen(result.error),!progress.cancel);}
                layout();if(closing) DestroyWindow(hwnd);
            } else {
                const auto total=progress.total.load(),done=progress.done.load();
                if(total && !progress.cancel) status(L"Exporting textures: "+std::to_wstring(std::min(done,total))+L" of "+std::to_wstring(total)+L"  ("+std::to_wstring(std::min(size_t(100),done*100/total))+L"%)\nYou can cancel without changing your game files.");
            }
        }
        if(importBusy()) finishImportJobs(hwnd);
        if(busy()) {RECT r{px(28),px(ProgressY),px(632),px(ProgressY+6)};InvalidateRect(hwnd,&r,FALSE);}
        return 0;
    case WM_CLOSE:
        if(job.valid()) {closing=true;progress.cancel=true;status(L"Cancelling export before closing...");}
        else if(importBusy()) {closing=true;importMessage(L"Finishing the current step before closing...");}
        else DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
int batchImport(const fs::path& game,const fs::path& textures,bool apply) {
    const auto reviewed=textureimport::review(game,textures);
    if(!reviewed.error.empty()) {std::fprintf(stderr,"%s\n",reviewed.error.c_str());return 1;}
    std::printf("{\"changed\":%zu,\"unchanged\":%zu,\"missing\":%zu,\"restoredArchives\":%zu,\"canImport\":%s",reviewed.changed,reviewed.unchanged,reviewed.missing,reviewed.restoredArchives,reviewed.hasChanges()?"true":"false");
    if(apply) {
        const auto outcome=textureimport::apply(game,reviewed,runtimeFolder);
        if(!outcome.ok) {std::printf("}\n");std::fprintf(stderr,"%s\n",outcome.error.c_str());return 1;}
        std::printf(",\"imported\":%zu,\"resized\":%zu,\"notes\":%zu",outcome.imported,outcome.resized,outcome.notes.size());
        for(const auto& note:outcome.notes) std::fprintf(stderr,"%s\n",note.c_str());
    }
    std::printf("}\n");return 0;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDPIAware();CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    {wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);runtimeFolder=fs::path(path).parent_path()/"runtime";}
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);fs::path game,output,textures;bool batch=false,review=false,import=false;
    for(int i=1;i<count;++i) {std::wstring arg=args[i];
        if(arg==L"--export")batch=true;else if(arg==L"--review")review=true;else if(arg==L"--import")import=true;
        else if(arg==L"--install" && i+1<count)game=args[++i];else if(arg==L"--output" && i+1<count)output=args[++i];
        else if(arg==L"--textures" && i+1<count)textures=args[++i];else if(arg==L"--runtime" && i+1<count)runtimeFolder=args[++i];
        else if(arg==L"--compression" && i+1<count){std::wstring value=args[++i];if(value==L"fast")selectedCompression=0;else if(value==L"balanced")selectedCompression=1;else if(value==L"smallest")selectedCompression=2;else {std::fprintf(stderr,"Use --compression fast, balanced or smallest\n");LocalFree(args);CoUninitialize();return 2;}}
        else if(arg==L"--ui-scale" && i+1<count)scale=std::clamp(float(_wtof(args[++i])),1.f,2.f);}
    LocalFree(args);
    bool detected=false;
    if(game.empty()) {try {const auto environment=forge::env::Environment::detect();if(environment.installValid && fs::is_regular_file(environment.installDir/"data/graphics/pc/textures.big")) {game=environment.installDir;detected=true;}}catch(...) {}}
    if(batch) {const auto result=texturecollection::exportInstallation(game,output,nullptr,compressionLevels[selectedCompression]);if(!result.ok)std::fprintf(stderr,"%s\n",result.error.c_str());CoUninitialize();return result.ok?0:1;}
    if(review || import) {const int code=batchImport(game,textures,import);CoUninitialize();return code;}
    PWSTR pictures=nullptr;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures,0,nullptr,&pictures))) {
        if(output.empty()) output=pictures;
        if(textures.empty() && fs::is_regular_file(fs::path(pictures)/L"Fable textures"/"texture-project.json")) textures=fs::path(pictures)/L"Fable textures";
        CoTaskMemFree(pictures);
    }
    if(scale==1) {HDC dc=GetDC(nullptr);scale=float(GetDeviceCaps(dc,LOGPIXELSX))/96;ReleaseDC(nullptr,dc);}
    background=CreateSolidBrush(Bg);field=CreateSolidBrush(Panel);
    WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=background;cls.lpszClassName=L"FableTextureExport";RegisterClassW(&cls);
    RECT rectangle{0,0,px(660),px(Height)};const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN;AdjustWindowRect(&rectangle,style,FALSE);
    window=CreateWindowW(cls.lpszClassName,L"Fable Textures",style,CW_USEDEFAULT,CW_USEDEFAULT,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,nullptr,nullptr,instance,nullptr);
    if(!game.empty()) SetWindowTextW(gameInput,game.c_str());if(!output.empty())SetWindowTextW(outputInput,output.c_str());if(!textures.empty())SetWindowTextW(texturesInput,textures.c_str());
    if(detected) status(L"Found Fable: The Lost Chapters. Check the export location, then choose Export textures. You can change either folder.");
    ShowWindow(window,show);UpdateWindow(window);MSG message;
    while(GetMessageW(&message,nullptr,0,0)>0) {if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    DeleteObject(body);DeleteObject(title);DeleteObject(small);DeleteObject(background);DeleteObject(field);CoUninitialize();return 0;
}
