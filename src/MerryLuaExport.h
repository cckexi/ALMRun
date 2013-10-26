#include "MerryCommandManager.h"
#include "MerryError.h"
#include "MerryInformationDialog.h"
#include "MerryHotkey.h"
#include "MerryApp.h"
#include "MerryController.h"
#include "MerryTimerManager.h"
#include "MerryMacHelper.h"
#include "ALMRunConfig.h"
#include <wx/clipbrd.h>
#include <wx/stdpaths.h>
#include "dlgconfig.h"

static void* lua_tohwnd(lua_State* L,int index)
{
	return lua_isnumber(L, index)?(void*)lua_tointeger(L,index):lua_touserdata(L,index);
}

static int LuaAddCommand(lua_State* L)
{
	if (!lua_istable(L, 1))
		luaL_error(L, "bad argument #1 to 'addCommand' (table expected)");

	lua_pushstring(L, "name");
	lua_rawget(L, 1);
	wxString commandName(wxString(lua_tostring(L, -1), wxConvLocal));

	lua_pushstring(L, "key");
	lua_rawget(L, 1);
	wxString triggerKey(wxString(lua_tostring(L, -1), wxConvLocal));

	if (commandName.empty() && triggerKey.empty())
	{
		lua_settop(L, 1);
		luaL_error(L, "Command name or key not found.\r\n\r\n参数错误,丢失name或key参数.");
	}

	lua_pushstring(L, "desc");
	lua_rawget(L, 1);
	wxString commandDesc(wxString(lua_tostring(L, -1), wxConvLocal));

	lua_pushstring(L, "cmd");
	lua_rawget(L, 1);
	wxString commandLine(wxString(lua_tostring(L, -1), wxConvLocal));

	lua_pushstring(L, "flags");
	lua_rawget(L,1);
	int flags = lua_tointeger(L,-1);

	lua_pushstring(L, "func");
	lua_rawget(L, 1);
	int funcRef = 0;
	if (!lua_isfunction(L, -1))
	{
		if (commandLine.empty())
		{
			lua_settop(L, 1);
			luaL_error(L, "can't find the command or func\r\n\r\n参数错误,cmd或func参数未设置.");
			return 0;
		}
	}
	else
	{
		funcRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	int order = 0;
	if (!commandName.empty())
	{
		lua_getglobal(L, "GetCmdOrder");
		if (!lua_isnil(L, 1))
		{
			lua_pushstring(L, commandName.c_str());
			if (lua_pcall(L, 1, 1, 0) == 0)
				order = lua_tointeger(L,-1);

		}
		lua_pop(L, 1);
	}
	const int commandID = g_commands->AddCommand(commandName, commandDesc,commandLine,funcRef, triggerKey,order,(flags<<3) | CMDS_FLAG_LUA);

	if (commandID < 0 )
	{
		luaL_unref(L, LUA_REGISTRYINDEX, funcRef);
		lua_settop(L, 1);
		return luaL_error(L, ::MerryGetLastError().c_str());
	}

	if (!g_hotkey->RegisterHotkey(commandID))
	{
		lua_settop(L, 1);
		return luaL_error(L, ::MerryGetLastError().c_str());
	}

	lua_settop(L, 1);
	lua_pushnumber(L, commandID);
	return 1;
}

static int LuaEnableCommandKey(lua_State* L)
{
	if (lua_isnil(L, 1))
		return 0;
	if (!g_hotkey->RegisterHotkey(lua_tonumber(L, 1)))
		luaL_error(L, ::MerryGetLastError().c_str());
	return 0;
}

static int LuaDisableCommandKey(lua_State* L)
{
	if (!lua_isnil(L, 1))
		g_hotkey->UnregisterHotkey(lua_tonumber(L, 1));
	return 0;
}

static int LuaShellExecute(lua_State* L)
{
#ifdef __WXMSW__
	static bool chkwow = (
	#ifdef _ALMRUN_CONFIG_H_
		g_config->config[DisableWow64FsRedirection] && 
	#endif//ifdef _ALMRUN_CONFIG_H_
		wxIsPlatform64Bit() && wxGetWinVersion() >= wxWinVersion_6);
	PVOID wowOld = NULL;
#endif
	wxString commandName(wxString(lua_tostring(L, 1), wxConvLocal));
	wxString commandArg(wxString(lua_tostring(L, 2), wxConvLocal));
	wxString workingDir(wxString(lua_tostring(L, 3), wxConvLocal));
	wxString show(wxString(lua_tostring(L, 4), wxConvLocal));
#ifdef __WXMSW__
	if (chkwow)
		Wow64Disable(&wowOld);
#endif
	lua_pushboolean(L, g_controller->ShellExecute(commandName, commandArg, workingDir, show));
#ifdef __WXMSW__
	if (chkwow)
		Wow64Revert(wowOld);
#endif
	return 1;
}

static void* GetWindow(lua_State* L)
{
	void* window = lua_tohwnd(L, 1);
	if (!window)
		window = g_controller->GetForegroundWindow();
	return window;
}

static int LuaGetForegroundWindow(lua_State* L)
{
	void* window = g_controller->GetForegroundWindow();
	if (window)
		lua_pushlightuserdata(L, window);
	else
		lua_pushnil(L);
	return 1;
}

static int LuaSetForegroundWindow(lua_State* L)
{
	g_controller->SetForegroundWindow(lua_tohwnd(L, 1));
	return 0;
}


static int LuaToggleMerry(lua_State* L)
{
	MerryFrame& frame = ::wxGetApp().GetFrame();
#ifdef __WXOSX__
	if (!frame.IsShown())
		MerryActivateIgnoringOtherApps();
#endif
	frame.IsShown() ? frame.Hide() : frame.Show();
	return 0;
}

static int LuaShowWindow(lua_State* L)
{
	void* window = lua_tohwnd(L, 1);
	wxString show(wxString(lua_tostring(L, 2), wxConvLocal));
	g_controller->ShowWindow(window, show);
	return 0;
}

static int LuaCloseWindow(lua_State* L)
{
	g_controller->CloseWindow(GetWindow(L));
	return 0;
}

static int LuaIsWindowMax(lua_State* L)
{
	void* window = lua_tohwnd(L, 1);
	lua_pushboolean(L, g_controller->IsWindowMax(window));
	return 1;
}

static int LuaIsWindowMin(lua_State* L)
{
	void* window = lua_tohwnd(L, 1);
	lua_pushboolean(L, g_controller->IsWindowMin(window));
	return 1;
}

static int LuaIsWindowShown(lua_State* L)
{
	void* window = lua_tohwnd(L, 1);
	lua_pushboolean(L, g_controller->IsWindowShown(window));
	return 1;
}

static int LuaGetWindowText(lua_State* L)
{
	wxString windowText = g_controller->GetWindowText(GetWindow(L));
	lua_pushstring(L, windowText.c_str());
	return 1;
}

static int LuaSetWindowText(lua_State* L)
{
	wxString windowText(wxString(lua_tostring(L, 2), wxConvLocal));
	g_controller->SetWindowText(GetWindow(L), windowText);
	return 0;
}

static int LuaGetWindowSize(lua_State* L)
{
	int width, height;
	g_controller->GetWindowSize(GetWindow(L), width, height);
	lua_pushnumber(L, width);
	lua_pushnumber(L, height);
	return 2;
}

static int LuaSetWindowSize(lua_State* L)
{
	int width = lua_tonumber(L, 2);
	int height = lua_tonumber(L, 3);
	g_controller->SetWindowSize(GetWindow(L), width, height);
	return 0;
}

static int LuaGetWindowPosition(lua_State* L)
{
	int x, y;
	g_controller->GetWindowPosition(GetWindow(L), x, y);
	lua_pushnumber(L, x);
	lua_pushnumber(L, y);
	return 2;
}

static int LuaSetWindowPosition(lua_State* L)
{
	void* window = GetWindow(L);
	int x = lua_tonumber(L, 2);
	int y = lua_tonumber(L, 3);
	g_controller->SetWindowPosition(window, x, y);
	return 0;
}

static int LuaSetWindowPos(lua_State* L)
{
	UINT uFlags = SWP_NOSIZE | SWP_NOMOVE;// | SWP_NOZORDER;
	int x = 0,y = 0,cx = 0,cy = 0;
	void *hWnd,*hWndInsertAfter = (void*)0;
	int top = lua_gettop(L);
	switch(top)
	{
		case 7:
			uFlags = lua_tointeger(L,7);
		case 6:
			cy = lua_tointeger(L,6);
		case 5:
			cx = lua_tointeger(L,5);
			if (top < 7)
				uFlags ^= SWP_NOSIZE;
		case 4:
			y = lua_tointeger(L,4);
		case 3:
			x = lua_tointeger(L,3);
			if (top < 7)
				uFlags ^= SWP_NOMOVE;
		case 2:
			hWndInsertAfter = lua_tohwnd(L,2);
			hWnd = lua_tohwnd(L,1);
			break;
		default:
			lua_pushboolean(L,false);
			return 1;
	}
	lua_pushinteger(L,g_controller->SetWindowPos(hWnd,hWndInsertAfter,x,y,cx,cy,uFlags));
	return 1;
}

static int LuaFindWindow(lua_State* L)
{
	wxString className,windowName;
	void *parentWindow = NULL,*window = NULL;
	if (lua_type(L,2) == LUA_TSTRING)
	{//新的参数和WINDOWS API保持一致
	//	FindWindow(className,WindowName);
		className = wxString(lua_tostring(L,1),wxConvLocal);
		windowName = wxString(lua_tostring(L,2),wxConvLocal);
	}
	else
	{
		//兼容旧版本的FindWindow(name,parentWindow);
		windowName = wxString(lua_tostring(L,1),wxConvLocal);
		parentWindow = lua_tohwnd(L,2);
	}
	window = g_controller->FindWindowEx(parentWindow,NULL,className,windowName);
	if (window)
		lua_pushlightuserdata(L,window);
	else
		lua_pushnil(L);
	return 1;
}

static int LuaFindWindowEx(lua_State* L)
{
	int top = lua_gettop(L);
	void *window = NULL;
	if (top == 4)
	{
		void *Parent = lua_tohwnd(L,1);
		void *Child = lua_tohwnd(L,2);
		wxString ClassName(wxString(lua_tostring(L, 1), wxConvLocal));
		wxString WindowName(wxString(lua_tostring(L, 1), wxConvLocal));
		window = g_controller->FindWindowEx(Parent,Child,ClassName,WindowName);
	}
	if (window)
		lua_pushlightuserdata(L,window);
	else
		lua_pushnil(L);
	return 1;
}

static int LuaGetMousePosition(lua_State* L)
{
	int x, y;
	g_controller->GetMousePosition(x, y);
	lua_pushnumber(L, x);
	lua_pushnumber(L, y);
	return 2;
}

static int LuaSetMousePosition(lua_State* L)
{
	int x = lua_tonumber(L, 1);
	int y = lua_tonumber(L, 2);
	g_controller->SetMousePosition(x, y);
	return 0;
}

static int LuaMessage(lua_State* L)
{
	wxString title,message;
	if (!lua_isstring(L,2))
	{
		title = wxT("Message");
		message = wxString(lua_tostring(L, -1),wxConvLocal);
	}
	else
	{
		title = wxString(lua_tostring(L, 2),wxConvLocal);
		message = wxString(lua_tostring(L, 1),wxConvLocal);
	}
	new MerryInformationDialog(title, message);
	return 0;
}

static int LuaMessageBox(lua_State* L)
{
	long style = 5;
	wxString message,caption;
	int top = lua_gettop(L);
	switch(top)
	{
		case 3:
			 if (lua_isnumber(L, 3))
				style = lua_tointeger(L,3);
		case 2:
				caption = wxString(lua_tostring(L, 2), wxConvLocal);
		case 1:
				message = wxString(lua_tostring(L,1), wxConvLocal);
	}
	if (caption.empty())
		caption = wxMessageBoxCaptionStr;
	lua_pushnumber(L,wxMessageBox(message,caption,style));
	wxYES_NO;
	return 1;
}

static int LuaInputBox(lua_State* L)
{
	wxString message(wxString(lua_tostring(L, 1), wxConvLocal));
	wxString caption(wxString(lua_tostring(L, 2), wxConvLocal));
	wxString value(wxString(lua_tostring(L, 3), wxConvLocal));
	long style = wxCENTRE | wxOK;
	if (caption.empty())
		caption = wxGetTextFromUserPromptStr;
	if (value.GetChar(0) == '*')
	{
		style |= wxTE_PASSWORD;
		value.Clear();
	}
	wxTextEntryDialog* dlg = new wxTextEntryDialog(NULL,message,caption,value,style);
	if (dlg->ShowModal() == wxID_OK)
		lua_pushstring(L,dlg->GetValue().c_str());
	else
		lua_pushstring(L,NULL);
	dlg->Destroy();
	return 1;
}

static int LuaEnterKey(lua_State* L)
{
	wxArrayString args;
	for (int i=1; i<=lua_gettop(L); ++i)
		args.push_back(wxString(lua_tostring(L, i), wxConvLocal));
	g_controller->EnterKey(args);
	return 0;
}

static int LuaEnterText(lua_State* L)
{
	wxString text(lua_tostring(L, 1), wxConvLocal);
	g_controller->EnterText(text);
	return 0;
}

static int LuaSetTimer(lua_State* L)
{
	if (!lua_isfunction(L, 3))
		luaL_error(L, "can't find the timer callback");

	int millisecond = lua_tonumber(L, 1);
	bool oneShot = lua_toboolean(L, 2) != 0;
	lua_pushvalue(L, 3);
	void* handler = g_timers->SetTimer(millisecond, oneShot, luaL_ref(L, LUA_REGISTRYINDEX));
	lua_pushlightuserdata(L, handler);

	return 1;
}

static int LuaClearTimer(lua_State* L)
{
	g_timers->ClearTimer(lua_touserdata(L, 1));
	return 0;
}

static int LuaDir(lua_State* L)
{
	wxString ls_path,filespec;
	int sub = -1;
	int top = lua_gettop(L);
	switch(top)
	{
		case 3:
			 if (lua_isnumber(L, 3))
				 sub = lua_tointeger(L,3);
		case 2:
			if (lua_isstring(L,2))
				filespec = wxString(lua_tostring(L, 2), wxConvLocal);
		case 1:
			if (lua_isstring(L,1))
				ls_path = wxString(lua_tostring(L,1), wxConvLocal);
			break;
	}
	wxArrayString files;
#ifdef _ALMRUN_CONFIG_H_
	g_config->ListFiles(ls_path,&files,filespec,wxEmptyString,sub);
#endif//ifdef _ALMRUN_CONFIG_H_
	lua_newtable(L);
	int i = files.Count();
	for(int j = 0;j<i;++j)
	{
		lua_pushinteger(L,j);
		lua_pushstring(L,files[j]);
		lua_settable(L,-3);
	}
	files.Clear();
	return 1;
}
#ifdef __WXMSW__

static int LuaSHSpecialFolders(lua_State* L)
{
	lua_pushstring(L,wxStandardPaths::MSWGetShellDir(lua_tointeger(L,-1)));
	return 1;
}

static int LuaSHEmptyRecycleBin(lua_State* L)
{
	DWORD Flags = lua_tointeger(L,3);
	wxString RootPath = wxString(lua_tostring(L,2),wxConvLocal);
	HWND hwnd = (HWND)lua_tohwnd(L,1);
	lua_pushinteger(L,SHEmptyRecycleBin(hwnd,RootPath.c_str(),Flags));
	return 1;
}

static int LuaEmptyRecycleBin(lua_State* L)
{
	DWORD Flags = 0;
	if (lua_tointeger(L,-1) == 1)
		Flags = SHERB_NOCONFIRMATION;
	lua_pushinteger(L,SHEmptyRecycleBin(NULL,NULL,Flags));
	return 1;
}

#endif

static int LuaSetEnv(lua_State* L)
{
	wxString var(lua_tostring(L, 1), wxConvLocal);
	wxString value(lua_tostring(L, 2), wxConvLocal);
	lua_pushboolean(L,::wxSetEnv(var,value));
	return 1;
}

static int LuaGetEnv(lua_State* L)
{
	wxString var(lua_tostring(L, 1), wxConvLocal);
	if (!::wxGetEnv(var,&var))
		var.Clear();
	lua_pushstring(L,var.c_str());
	return 1;
}

static int LuaSetClipboardData(lua_State* L)
{
	if (wxTheClipboard->Open())
	{
		lua_pushboolean(L, wxTheClipboard->SetData(new wxTextDataObject(lua_tostring(L,1))));
		wxTheClipboard->Close();
	}
	else
		lua_pushboolean(L, 0);
	return 1;
}

static int LuaGetClipboardData(lua_State* L)
{
	int ret = 0;
	if (wxTheClipboard->Open())
	{
		if (wxTheClipboard->IsSupported(wxDF_TEXT))
		{
			wxTextDataObject data;
			wxTheClipboard->GetData(data);
			lua_pushstring(L,data.GetText());
			++ret;
		}
		wxTheClipboard->Close();
	}
	return ret;
}

static int LuaConfig(lua_State* L)
{
#ifdef _ALMRUN_CONFIG_H_
	int nIndex = lua_gettop(L);
	lua_pushnil(L);
	while( 0 != lua_next(L, nIndex ) )
	{		
		// 现在栈顶（-1）是 value，-2 位置是对应的 key
		if (lua_isnumber(L,-2))
			return 0;
		if (lua_isnumber(L,-1))//数值型/布尔型的配置
			g_config->set(lua_tostring(L,-2),lua_tointeger(L,-1));
		else//字符串的配置
			g_config->set(lua_tostring(L,-2),wxString(lua_tostring(L, -1), wxConvLocal));
		lua_pop(L, 1 );  // 弹出 value，让 key 留在栈顶
	}
#endif//ifdef _ALMRUN_CONFIG_H_
	return 0;
}

static int LuaTestConfig(lua_State* L)
{
#ifdef _ALMRUN_CONFIG_H_
	g_config->GuiConfig();
#endif//ifdef _ALMRUN_CONFIG_H_
	return 0;
}

static int LuaFileExists(lua_State* L)
{
	lua_pushboolean(L,wxFileExists(wxString(lua_tostring(L, -1), wxConvLocal)));
	return 1;
}

static int LuaDirExists(lua_State* L)
{
	lua_pushboolean(L,wxDirExists(wxString(lua_tostring(L, -1), wxConvLocal)));
	return 1;
}

static int LuaReConfig(lua_State* L)
{
	::wxGetApp().GetFrame().NewConfig();
	return 0;
}
