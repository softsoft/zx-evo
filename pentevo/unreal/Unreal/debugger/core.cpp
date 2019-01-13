#include "std.h"
#include "core.h"
#include "resource.h"
#include "vars.h"
#include "consts.h"
#include "op_system.h"
#include "dx.h"
#include "dxrend.h"
#include "util.h"
#include "config.h"
#include "libs/cpu_manager.h"
#include "libs/dbglabls.h"
#include "emulkeys.h"
#include "funcs.h"
#include "wd93dat.h"
#include "cheat.h"
#include "snapshot.h"

char bpx_file_name[FILENAME_MAX];
char str[80]{};

namespace z80dbg
{
	__int64 __cdecl delta()
	{
		return comp.t_states + cpu.t - cpu.debug_last_t;
	}

	void __cdecl SetLastT()
	{
		cpu.debug_last_t = comp.t_states + cpu.t;
	}

}

DebugCore* DebugCore::instance_ = nullptr;
ActionManager* ActionManager::instance_ = nullptr;

DebugCore::DebugCore() : ref_(*this)
{
	create_window();
	view_ = new DebugView(wnd_);
	mem_ = new MemView(ref_, *view_);
	dialogs_ = new Dialogs(*view_, *mem_);
	regs_ = new RegView(ref_, *view_, *mem_);
	watch_ = new WatchView(ref_, *view_);

	stack_ = new StackView(ref_, *view_);
	ay_ = new AyView(ref_, *view_);
	banks_ = new BanksView(ref_, *view_);
	ports_ = new PortsView(ref_, *view_);
	dos_ = new DosView(ref_, *view_);
	time_ = new TimeView(ref_, *view_);
	trace_ = new TraceView(ref_, *view_, *mem_);
	tsconf_ = new TsconfView(*view_);

	subscrible();
}

auto APIENTRY DebugCore::wnd_proc(HWND hwnd, UINT uMessage, WPARAM wparam, LPARAM lparam) -> LRESULT
{
	const auto core = get_instance();

	if (uMessage == WM_CLOSE)
	{
		core->mon_emul();
		return 0;
	}

	if (uMessage == WM_PAINT)
	{
		const auto view = get_view();
		view->on_paint(hwnd);
		return 0L;
	}

	if (uMessage == WM_COMMAND)
	{
		switch (wparam) {
		case IDM_DEBUG_RUN: core->mon_emul(); break;

		case IDM_DEBUG_STEP: core->mon_step(); break;
		case IDM_DEBUG_STEPOVER: core->mon_stepover(); break;
		case IDM_DEBUG_TILLRETURN: core->mon_exitsub(); break;
		case IDM_DEBUG_RUNTOCURSOR: ActionManager::invoke(ActionType::trace, "hera"); break;

		case IDM_BREAKPOINT_TOGGLE: ActionManager::invoke(ActionType::trace, "bpx"); break;
		case IDM_BREAKPOINT_MANAGER: get_dialogs()->mon_bpdialog(); break;

		case IDM_MON_LOADBLOCK: core->mon_load(); break;
		case IDM_MON_SAVEBLOCK: core->mon_save(); break;
		case IDM_MON_FILLBLOCK: core->mon_fill(); break;

		case IDM_MON_RIPPER: core->mon_tool(); break;
		default:;
		}
		needclr = 1;
	}

	return DefWindowProc(hwnd, uMessage, wparam, lparam);
}

auto DebugCore::rw_err(const char* msg) -> void
{
	MessageBox(wnd, msg, "Error", MB_OK | MB_ICONERROR);
}

auto DebugCore::subscrible() -> void
{
	ActionManager::subscrible(ActionType::monitor, "emul", [this]() { mon_emul(); });
	ActionManager::subscrible(ActionType::monitor, "saveblock", [this]() { mon_save(); });
	ActionManager::subscrible(ActionType::monitor, "loadblock", [this]() { mon_load(); });
	ActionManager::subscrible(ActionType::monitor, "fillblock", [this]() { mon_fill(); });
	ActionManager::subscrible(ActionType::monitor, "step", [this]() { mon_step(); });
	ActionManager::subscrible(ActionType::monitor, "stepover", [this]() { mon_stepover(); });
	ActionManager::subscrible(ActionType::monitor, "exitsub", [this]() { mon_exitsub(); });

	ActionManager::subscrible(ActionType::monitor, "dump", [this]()
	{
		mem_->mem_dump = (mem_->mem_dump + 1) & 1;
		mem_->mem_sz = mem_->mem_dump ? 32 : 8;
	});

	ActionManager::subscrible(ActionType::monitor, "switchdump", [this]() { mon_switch_dump(); });
	ActionManager::subscrible(ActionType::monitor, "next", [this]() { mon_nxt(); });
	ActionManager::subscrible(ActionType::monitor, "prev", [this]() { mon_prv(); });
	ActionManager::subscrible(ActionType::monitor, "rip", [this]() { mon_tool(); });
	ActionManager::subscrible(ActionType::monitor, "help", []() { showhelp("monitor_keys"); });
	ActionManager::subscrible(ActionType::monitor, "cpu", [this]() { mon_switch_cpu(); });
	ActionManager::subscrible(ActionType::monitor, "exit", []() { correct_exit(); });

	// global command 
	// todo move out
	ActionManager::subscrible(ActionType::monitor, "pokedialog", []() { main_poke(); });
	ActionManager::subscrible(ActionType::monitor, "tapebrowser", []() { main_tapebrowser(); });
	ActionManager::subscrible(ActionType::monitor, "reset", []() { main_reset(); });
	ActionManager::subscrible(ActionType::monitor, "reset128", []() { main_reset128(); });
	ActionManager::subscrible(ActionType::monitor, "resetsys", []() { main_resetsys(); });
	ActionManager::subscrible(ActionType::monitor, "reset48", []() { main_reset48(); });
	ActionManager::subscrible(ActionType::monitor, "resetbasic", []() { main_resetbas(); });
	ActionManager::subscrible(ActionType::monitor, "resetdos", []() { main_resetdos(); });
	ActionManager::subscrible(ActionType::monitor, "resetcache", []() { main_resetcache(); });
	ActionManager::subscrible(ActionType::monitor, "nmi", []() { main_nmi(); });
	ActionManager::subscrible(ActionType::monitor, "nmidos", []() { main_nmidos(); });
	ActionManager::subscrible(ActionType::monitor, "nmicache", []() { main_nmicache(); });
	ActionManager::subscrible(ActionType::monitor, "save", []() { savesnap(); });
	ActionManager::subscrible(ActionType::monitor, "load", []() { opensnap(); });
	ActionManager::subscrible(ActionType::monitor, "savesound", []() { savesnddialog(); });
	ActionManager::subscrible(ActionType::monitor, "qsave1", []() { qsave("qsave1.sna"); });
	ActionManager::subscrible(ActionType::monitor, "qsave2", []() { qsave("qsave2.sna"); });
	ActionManager::subscrible(ActionType::monitor, "qsave3", []() { qsave("qsave3.sna"); });
	ActionManager::subscrible(ActionType::monitor, "qload1", []() { qload("qsave1.sna"); });
	ActionManager::subscrible(ActionType::monitor, "qload2", []() { qload("qsave2.sna"); });
	ActionManager::subscrible(ActionType::monitor, "qload3", []() { qload("qsave3.sna"); });
	ActionManager::subscrible(ActionType::monitor, "labels", []() { mon_show_labels(); });
	ActionManager::subscrible(ActionType::monitor, "memsearch", []() { main_cheat(); });

	ActionManager::subscrible(ActionType::main, "exit", []() { correct_exit(); });
	ActionManager::subscrible(ActionType::main, "monitor", []() { main_debug(); });
	ActionManager::subscrible(ActionType::main, "fullscreen", []() { main_fullscr(); });
	ActionManager::subscrible(ActionType::main, "pause", []() { main_pause(); });
	ActionManager::subscrible(ActionType::main, "selectfix", []() { main_selectfix(); });
	ActionManager::subscrible(ActionType::main, "selectsnd", []() { main_selectsnd(); });
	ActionManager::subscrible(ActionType::main, "incfix", []() { main_incfix(); });
	ActionManager::subscrible(ActionType::main, "decfix", []() { main_decfix(); });
	ActionManager::subscrible(ActionType::main, "incfix10", []() { main_incfix10(); });
	ActionManager::subscrible(ActionType::main, "decfix10", []() { main_decfix10(); });

	ActionManager::subscrible(ActionType::main, "leds", []() { main_leds(); });
	ActionManager::subscrible(ActionType::main, "status", []() { main_status(); });
	ActionManager::subscrible(ActionType::main, "maxspeed", []() { main_maxspeed(); });
	ActionManager::subscrible(ActionType::main, "selectfilter", []() { main_selectfilter(); });
	ActionManager::subscrible(ActionType::main, "selectdriver", []() { main_selectdriver(); });
	ActionManager::subscrible(ActionType::main, "pokedialog", []() { main_poke(); });
	ActionManager::subscrible(ActionType::main, "starttape", []() { main_starttape(); });
	ActionManager::subscrible(ActionType::main, "screenshot", []() { main_scrshot(); });
	ActionManager::subscrible(ActionType::main, "savevideo", []() { main_savevideo(); });

	ActionManager::subscrible(ActionType::main, "reset", []() { main_reset(); });
	ActionManager::subscrible(ActionType::main, "reset128", []() { main_reset128(); });
	ActionManager::subscrible(ActionType::main, "resetsys", []() { main_resetsys(); });
	ActionManager::subscrible(ActionType::main, "reset48", []() {main_reset48(); });
	ActionManager::subscrible(ActionType::main, "resetbasic", []() {main_resetbas(); });
	ActionManager::subscrible(ActionType::main, "resetdos", []() {main_resetdos(); });
	ActionManager::subscrible(ActionType::main, "resetcache", []() {main_resetcache(); });

	ActionManager::subscrible(ActionType::main, "nmi", []() { main_nmi(); });
	ActionManager::subscrible(ActionType::main, "nmidos", []() { main_nmidos(); });
	ActionManager::subscrible(ActionType::main, "nmicache", []() { main_nmicache(); });

	ActionManager::subscrible(ActionType::main, "tapebrowser", []() { main_tapebrowser(); });
	ActionManager::subscrible(ActionType::main, "settings", []() { setup_dlg(); });
	ActionManager::subscrible(ActionType::main, "save", []() {savesnap(); });
	ActionManager::subscrible(ActionType::main, "load", []() {opensnap(); });
	ActionManager::subscrible(ActionType::main, "savesound", []() {savesnddialog(); });
	ActionManager::subscrible(ActionType::main, "qsave1", []() {qsave1(); });
	ActionManager::subscrible(ActionType::main, "qsave2", []() {qsave2(); });
	ActionManager::subscrible(ActionType::main, "qsave3", []() {qsave3(); });
	ActionManager::subscrible(ActionType::main, "qload1", []() {qload1(); });
	ActionManager::subscrible(ActionType::main, "qload2", []() {qload2(); });
	ActionManager::subscrible(ActionType::main, "qload3", []() {qload3(); });

	ActionManager::subscrible(ActionType::main, "keystick", []() {main_keystick(); });
	ActionManager::subscrible(ActionType::main, "autofire", []() {main_autofire(); });
	ActionManager::subscrible(ActionType::main, "saveram", []() {main_save_ram(); });
	ActionManager::subscrible(ActionType::main, "saveall", []() {main_save(); });
	ActionManager::subscrible(ActionType::main, "lockmouse", []() {main_mouse(); });
	ActionManager::subscrible(ActionType::main, "xtkbd", []() {main_atmkbd(); });
	ActionManager::subscrible(ActionType::main, "pastetext", []() {main_pastetext(); });
	ActionManager::subscrible(ActionType::main, "size1", []() {main_size1(); });
	ActionManager::subscrible(ActionType::main, "size2", []() {main_size2(); });
	ActionManager::subscrible(ActionType::main, "sizem", []() {main_sizem(); });
	ActionManager::subscrible(ActionType::main, "memsearch", []() {main_cheat(); });
	ActionManager::subscrible(ActionType::main, "help", []() { showhelp(); });
	ActionManager::subscrible(ActionType::main, "tsutoggle", []() {main_tsutoggle(); });
	ActionManager::subscrible(ActionType::main, "flictoggle", []() {main_flictoggle(); });

	ActionManager::subscrible(ActionType::xt, "flictoggle", []() {main_atmkbd(); });
}

auto DebugCore::create_window() -> void
{
	WNDCLASS  wc{};
	RECT cl_rect;
	const DWORD dw_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

	wc.lpfnWndProc = WNDPROC(wnd_proc);
	wc.hInstance = hIn;
	wc.lpszClassName = "DEBUG_WND";
	wc.hIcon = LoadIcon(hIn, MAKEINTRESOURCE(IDI_MAIN));
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClass(&wc);

	menu_ = LoadMenu(hIn, MAKEINTRESOURCE(IDR_DEBUGMENU));

	wnd_ = CreateWindow("DEBUG_WND", "UnrealSpeccy debugger", dw_style, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, menu_, hIn, NULL);

	cl_rect.left = 0;
	cl_rect.top = 0;
	cl_rect.right = DEBUG_WND_WIDTH - 1;
	cl_rect.bottom = DEBUG_WND_HEIGHT - 1;
	AdjustWindowRect(&cl_rect, dw_style, GetMenu(wnd_) != nullptr);
	SetWindowPos(wnd_, nullptr, 0, 0, cl_rect.right - cl_rect.left + 1, cl_rect.bottom - cl_rect.top + 1, SWP_NOMOVE);

	for (unsigned i = 0; i < 0x100; i++)
	{
		const unsigned y = (i & 8) ? 0xFF : 0xC0;
		const unsigned r = (i & 2) ? y : 0;
		const unsigned g = (i & 4) ? y : 0;
		const unsigned b = (i & 1) ? y : 0;

		gdibmp.header.bmiColors[i].rgbRed = r;
		gdibmp.header.bmiColors[i].rgbGreen = g;
		gdibmp.header.bmiColors[i].rgbBlue = b;
	}
}

auto DebugCore::mon_emul() const -> void
{
	for (unsigned i = 0; i < TCpuMgr::get_count(); i++)
	{
		auto& cpu = TCpuMgr::get_cpu(i);
		cpu.dbgchk = isbrk(cpu);
		cpu.dbgbreak = 0;
	}
	dbgbreak = 0;
}

auto DebugCore::mon_exitsub() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();
	cpu.dbgchk = 1;
	cpu.dbgbreak = 0;
	dbgbreak = 0;
	cpu.dbg_stophere = cpu.DirectRm(cpu.sp) + 0x100 * cpu.DirectRm(cpu.sp + 1);
}

auto DebugCore::mon_step() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();
	auto& prevcpu = TCpuMgr::prev_cpu();

	cpu.SetLastT();
	prevcpu = TZ80State(cpu);

	vid.memcyc_lcmd = 0; // new command, start accumulate number of busy memcycles

	cpu.Step();
	cpu.CheckNextFrame();

	// Baseconf NMI trap
	if (conf.mem_model == MM_ATM3 && (comp.pBF & 0x10) && (cpu.pc == comp.pBD))
		nmi_pending = 1;

	// NMI processing
	if (nmi_pending)
	{
		if (conf.mem_model == MM_ATM3)
		{
			nmi_pending = 0;
			cpu.nmi_in_progress = true;
			set_banks();
			m_nmi(RM_NOCHANGE);
		}
		else if (conf.mem_model == MM_PROFSCORP || conf.mem_model == MM_SCORP)
		{
			nmi_pending--;
			if (cpu.pc > 0x4000)
			{
				m_nmi(RM_DOS);
				nmi_pending = 0;
			}
		}
		else
			nmi_pending = 0;
	} // end if (nmi_pending)

	// Baseconf NMI
	if (comp.pBE)
	{
		if (conf.mem_model == MM_ATM3 && comp.pBE == 1)
		{
			cpu.nmi_in_progress = false;
			set_banks();
		}
		comp.pBE--;
	}

	// INT processing
	if (conf.mem_model == MM_TSL)
	{
		const bool vdos = comp.ts.vdos || comp.ts.vdos_m1;

		ts_frame_int(vdos);
		ts_line_int(vdos);
		ts_dma_int(vdos);

		cpu.int_pend = comp.ts.intctrl.pend && !vdos;
	} // Reset INT
	else
	{
		const unsigned int_start = conf.intstart;
		unsigned int_end = conf.intstart + conf.intlen;

		cpu.int_pend = false;
		if ((cpu.t >= int_start) && (cpu.t < int_end))
			cpu.int_pend = true;
		else if (int_end >= conf.frame)
		{
			int_end -= conf.frame;
			if ((cpu.t >= int_start) || (cpu.t < int_end))
				cpu.int_pend = true;
		}
	}

	if (cpu.int_pend && cpu.iff1 && cpu.t != cpu.eipos && cpu.int_gate)
	{
		handle_int(&cpu, cpu.IntVec());
	}
	update_screen(); // update screen, TSU, DMA

	cpu.trace_curs = cpu.pc;
}

auto DebugCore::mon_stepover() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();
	u8 trace = 1;

	// call,rst
	if (cpu.pc_trflags & TWF_CALLCMD)
	{
		cpu.dbg_stopsp = cpu.sp & 0xFFFF;
		cpu.dbg_stophere = cpu.nextpc;
		trace = 0;
	}
	else if (cpu.pc_trflags & TWF_BLKCMD) // ldir/lddr|cpir/cpdr|otir/otdr|inir/indr
	{
		trace = 0;
		cpu.dbg_stophere = cpu.nextpc;
	}

	if (trace)
	{
		mon_step();
	}
	else
	{
		cpu.dbgbreak = 0;
		dbgbreak = 0;
		cpu.dbgchk = 1;
	}
}

auto DebugCore::mon_switch_cpu() const -> void
{
	//    CpuMgr.CopyToPrev();
	auto& cpu0 = TCpuMgr::get_cpu();
	cpu0.dbgbreak = 0;
	TCpuMgr::switch_cpu();
	auto& cpu1 = TCpuMgr::get_cpu();

	if (cpu1.trace_curs == UINT_MAX)
		cpu1.trace_curs = cpu1.pc;
	if (cpu1.trace_top == UINT_MAX)
		cpu1.trace_top = cpu1.pc;
	if (cpu1.trace_mode == UINT_MAX)
		cpu1.trace_mode = 0;

	debugscr();
	view_->flip();
}

auto DebugCore::mon_nxt() -> void
{
	activedbg = (activedbg == dbgwnd::mem) ? dbgwnd::banks : dbgwnd(int(activedbg) - 1);
	mon_aux();
}

auto DebugCore::mon_aux() const -> void
{
	switch (activedbg)
	{
	case dbgwnd::banks:
		banks_->showbank = true;
		break;

	default:
		banks_->showbank = false;
		break;
	}
}

auto DebugCore::mon_prv() -> void
{
	activedbg = (activedbg == dbgwnd::banks) ? dbgwnd::mem : dbgwnd(int(activedbg) + 1);
	mon_aux();
}

auto DebugCore::mon_switch_dump() const -> void
{
	static const unsigned dump_modes[] = { ed_mem, ed_phys, ed_log, ed_cmos, ed_nvram };
	static unsigned idx = 0;
	++idx;
	idx %= ed_max;
	mem_->editor = dump_modes[idx];
}

auto DebugCore::mon_tool() -> void
{
	auto& cpu = TCpuMgr::get_cpu();
	static u8 unref = 0xCF;
	if (ripper) {
		OPENFILENAME ofn{};
		char savename[0x200]; *savename = 0;
		ofn.lStructSize = (WinVerMajor < 5) ? OPENFILENAME_SIZE_VERSION_400 : sizeof(OPENFILENAME);
		ofn.lpstrFilter = "Memory dump\0*.bin\0";
		ofn.lpstrFile = savename; ofn.nMaxFile = sizeof savename;
		ofn.lpstrTitle = "Save ripped data";
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
		ofn.hwndOwner = wnd;
		ofn.lpstrDefExt = "bin";
		ofn.nFilterIndex = 1;
		if (GetSaveFileName(&ofn)) {
			for (unsigned i = 0; i < 0x10000; i++)
				snbuf[i] = (cpu.membits[i] & ripper) ? cpu.DirectRm(i) : unref;
			const auto ff = fopen(savename, "wb");
			if (ff) fwrite(snbuf, 1, 0x10000, ff), fclose(ff);
		}
		ripper = 0;
	}
	else {
		view_->filledframe(tool_x, tool_y, 17, 6);
		view_->tprint(tool_x, tool_y, "  ripper's tool  ", frm_header);
		view_->tprint(tool_x + 1, tool_y + 2, "trace reads:", fframe_inside);
		*reinterpret_cast<unsigned*>(str) = 'Y';
		if (!view_->inputhex(tool_x + 15, tool_y + 2, 1, false)) return;
		view_->tprint(tool_x + 15, tool_y + 2, str, fframe_inside);
		if (*str == 'Y' || *str == 'y' || *str == '1') ripper |= MEMBITS_R;
		*reinterpret_cast<unsigned*>(str) = 'N';
		view_->tprint(tool_x + 1, tool_y + 3, "trace writes:", fframe_inside);
		if (!view_->inputhex(tool_x + 15, tool_y + 3, 1, false)) { ripper = 0; return; }
		view_->tprint(tool_x + 15, tool_y + 3, str, fframe_inside);
		if (*str == 'Y' || *str == 'y' || *str == '1') ripper |= MEMBITS_W;
		view_->tprint(tool_x + 1, tool_y + 4, "unref. byte:", fframe_inside);
		unsigned ub;
		if ((ub = view_->input2(tool_x + 14, tool_y + 4, unref)) == UINT_MAX) { ripper = 0; return; }
		unref = u8(ub);
		if (ripper)
			for (unsigned i = 0; i < 0x10000; i++)
				cpu.membits[i] &= ~(MEMBITS_R | MEMBITS_W);
	}
}

auto DebugCore::mon_fill() -> void
{
	view_->filledframe(6, 10, 26, 5);
	char ln[64];

	sprintf(ln, "start: %04X end: %04X", addr, end);
	view_->tprint(6, 10, "    fill memory block     ", frm_header);
	view_->tprint(7, 12, "pattern (hex):", fframe_inside);
	view_->tprint(7, 13, ln, fframe_inside);

	static char fillpattern[10] = "00";

	u8 pattern[4];
	unsigned fillsize = 0;

	strcpy(str, fillpattern);
	if (!view_->inputhex(22, 12, 8, true)) return;
	strcpy(fillpattern, str);

	if (!fillpattern[0])
		strcpy(fillpattern, "00");

	for (fillsize = 0; fillpattern[2 * fillsize]; fillsize++) {
		if (!fillpattern[2 * fillsize + 1]) fillpattern[2 * fillsize + 1] = '0', fillpattern[2 * fillsize + 2] = 0;
		pattern[fillsize] = hex(fillpattern + 2 * fillsize);
	}
	view_->tprint(22, 12, "        ", fframe_inside);
	view_->tprint(22, 12, fillpattern, fframe_inside);

	auto a1 = view_->input4(14, 13, addr); if (a1 == UINT_MAX) return;
	addr = a1;
	view_->tprint(14, 13, str, fframe_inside);
	a1 = view_->input4(24, 13, end); if (a1 == UINT_MAX) return;
	end = a1;

	unsigned pos = 0;
	for (a1 = addr; a1 <= end; a1++) {
		cpu.DirectWm(a1, pattern[pos]);
		if (++pos == fillsize) pos = 0;
	}
}

auto DebugCore::mon_load() -> void
{
	static MenuItem items[] =
	{ { "from binary file", MenuItem::left },
	  { "from TR-DOS file", MenuItem::left },
	  { "from TR-DOS sectors", MenuItem::left },
	  { "from raw sectors of FDD image", MenuItem::left } };
	static MenuDef menu = { items, 3, "Load data to memory...", 0 };

	if (!view_->handle_menu(&menu))
		return;

	u8 bf[0x10000];

	switch (menu.pos)
	{
	case 0:
	{
		if (!query_file_addr(FDM_LOAD))
			return;
		const auto ff = fopen(fname, "rb");
		if (!ff)
			return;
		const auto sz = fread(bf, 1, sizeof(bf), ff);
		fclose(ff);
		end = addr + sz - 1;
		end &= 0xFFFF;
		write_mem(bf);
		return;
	}

	case 1:
	{
		rw_err("file selector\r\nis not implemented");
		return;
	}

	case 2:
	{
		rw_trdos_sectors(FDM_LOAD, bf);
		return;
	}

	case 3:
	{
		return;
	}
	default:;
	}
}

auto DebugCore::mon_save() -> void
{
	static MenuItem items[] =
	{ { "to binary file", MenuItem::left },
	  { "to TR-DOS file", MenuItem::left },
	  { "to TR-DOS sectors", MenuItem::left },
	  { "as Z80 disassembly", MenuItem::left },
	  { "to raw sectors of FDD image", MenuItem::flags_t(MenuItem::left | MenuItem::disabled) } };
	static MenuDef menu = { items, 4, "Save data from memory...", 0 };

	if (!view_->handle_menu(&menu)) return;

	u8 bf[0x10000];

	switch (menu.pos)
	{
	case 0:
	{
		if (!query_file_addr(FDM_SAVE)) return;
		read_mem(bf);
		const auto ff = fopen(fname, "wb");
		if (!ff) return;
		fwrite(bf, 1, end + 1 - addr, ff);
		fclose(ff);
		return;
	}

	case 1:
	{
		wr_trdos_file(bf);
		return;
	}

	case 2:
	{
		rw_trdos_sectors(FDM_SAVE, bf);
		return;
	}

	case 3:
	{
		if (!query_file_addr(FDM_DISASM)) return;
		const auto ff = fopen(fname, "wt");
		if (!ff) return;
		for (auto a = addr; a <= end; ) {
			//            char line[64]; //Alone Coder 0.36.7
			char line[16 + 129]; //Alone Coder 0.36.7
			a += trace_->disasm_line(a, line);
			fprintf(ff, "%s\n", line);
		}
		fclose(ff);
		return;
	}

	case 4:
	{
		return;
	}
	default:;
	}
}

auto DebugCore::rw_trdos_sectors(FILEDLG_MODE mode, u8* memdata) -> char
{
	view_->filledframe(file_dlg_x, file_dlg_y, file_dlg_dx, 7);
	const auto title = (mode == FDM_LOAD) ? " Read from TR-DOS sectors" : " Write to TR-DOS sectors ";
	view_->tprint(file_dlg_x, file_dlg_y, title, frm_header);

	char ln[64];

	sprintf(ln, "trk (00-9F): %02X", rw_trk);
	view_->tprint(file_dlg_x + 1, file_dlg_y + 3, ln, fframe_inside);

	sprintf(ln, "sec (00-0F): %02X", rw_tsec);
	view_->tprint(file_dlg_x + 1, file_dlg_y + 4, ln, fframe_inside);

	sprintf(ln, "start: %04X end: %04X", addr, end);
	view_->tprint(file_dlg_x + 1, file_dlg_y + 5, ln, fframe_inside);

	if (!rw_select_drive()) return 0;
	FDD *fdd = &comp.wd.fdd[rw_drive];
	// if (fdd->sides != 2) { rw_err("single-side TR-DOS disks are not supported"); return 0; }

	unsigned t = view_->input2(file_dlg_x + 14, file_dlg_y + 3, rw_trk);
	if (t == UINT_MAX) return 0; else rw_trk = t;
	view_->fillattr(file_dlg_x + 14, file_dlg_y + 3, 2);

	t = view_->input2(file_dlg_x + 14, file_dlg_y + 4, rw_tsec);
	if (t == UINT_MAX) return 0; else rw_tsec = t;
	view_->fillattr(file_dlg_x + 14, file_dlg_y + 4, 2);

	t = view_->input4(file_dlg_x + 8, file_dlg_y + 5, addr);
	if (t == UINT_MAX) return 0; else addr = t;
	view_->fillattr(file_dlg_x + 8, file_dlg_y + 5, 4);

	for (;;) {
		t = view_->input4(file_dlg_x + 18, file_dlg_y + 5, end);
		view_->fillattr(file_dlg_x + 18, file_dlg_y + 5, 4);
		if (t == UINT_MAX) return 0;
		if (t < addr) continue;
		end = t; break;
	}

	unsigned offset = 0;
	if (mode == FDM_SAVE) read_mem(memdata);

	unsigned trk = rw_trk, sec = rw_tsec;

	TRKCACHE tc; tc.clear();
	for (;;) {
		int left = end + 1 - (addr + offset);
		if (left <= 0) break;
		if (left > 0x100) left = 0x100;

		tc.seek(fdd, trk / 2, trk & 1, LOAD_SECTORS);
		if (!tc.trkd) { sprintf(ln, "track #%02X not found", trk); rw_err(ln); break; }
		const SECHDR *hdr = tc.get_sector(sec + 1);
		if (!hdr || !hdr->data) { sprintf(ln, "track #%02X, sector #%02X not found", trk, sec); rw_err(ln); break; }
		if (hdr->l != 1) { sprintf(ln, "track #%02X, sector #%02X is not 256 bytes", trk, sec); rw_err(ln); break; }

		if (mode == FDM_LOAD) {
			memcpy(memdata + offset, hdr->data, left);
		}
		else {
			tc.write_sector(sec + 1, memdata + offset);
			fdd->optype |= 1;
		}

		offset += left;
		if (++sec == 0x10) trk++, sec = 0;
	}

	end = addr + offset - 1;
	if (mode == FDM_LOAD) write_mem(memdata);
	return 1;
}

auto DebugCore::wr_trdos_file(u8* memdata) -> char
{
	view_->filledframe(file_dlg_x, file_dlg_y, file_dlg_dx, 6);
	const auto title = " Write to TR-DOS file    ";
	view_->tprint(file_dlg_x, file_dlg_y, title, frm_header);

	char ln[64];

	sprintf(ln, "file:  %-8s %s", trdname, trdext);
	view_->tprint(file_dlg_x + 1, file_dlg_y + 3, ln, fframe_inside);

	sprintf(ln, "start: %04X end: %04X", addr, end);
	view_->tprint(file_dlg_x + 1, file_dlg_y + 4, ln, fframe_inside);

	if (!rw_select_drive()) return 0;
	auto fdd = &comp.wd.fdd[rw_drive];
	// if (fdd->sides != 2) { rw_err("single-side TR-DOS disks are not supported"); return 0; }

	strcpy(str, trdname);
	if (!view_->inputhex(file_dlg_x + 8, file_dlg_y + 3, 8, false)) return 0;
	view_->fillattr(file_dlg_x + 8, file_dlg_y + 3, 8);
	strcpy(trdname, str);
	for (int ptr = strlen(trdname); ptr < 8; trdname[ptr++] = ' ') {}
	trdname[8] = 0;

	strcpy(str, trdext);
	if (!view_->inputhex(file_dlg_x + 17, file_dlg_y + 3, 1, false)) return 0;
	view_->fillattr(file_dlg_x + 17, file_dlg_y + 3, 1);
	trdext[0] = str[0];
	trdext[1] = 0;

	auto t = view_->input4(file_dlg_x + 8, file_dlg_y + 4, addr);
	if (t == UINT_MAX) return 0; else addr = t;
	view_->fillattr(file_dlg_x + 8, file_dlg_y + 4, 4);

	for (;;) {
		t = view_->input4(file_dlg_x + 18, file_dlg_y + 4, end);
		view_->fillattr(file_dlg_x + 18, file_dlg_y + 4, 4);
		if (t == UINT_MAX) return 0;
		if (t < addr) continue;
		end = t; break;
	}

	read_mem(memdata);

	u8 hdr[16];
	memcpy(hdr, trdname, 8);
	hdr[8] = *trdext;

	const unsigned sz = end - addr + 1;
	*reinterpret_cast<u16*>(hdr + 9) = addr;
	*reinterpret_cast<u16*>(hdr + 11) = sz;
	hdr[13] = u8(align_by(sz, 0x100) / 0x100); // sector size

	fdd->optype |= 1;
	if (!fdd->addfile(hdr, memdata)) { rw_err("write error"); return 0; }
	return 1;
}

auto DebugCore::query_file_addr(const FILEDLG_MODE mode) -> char
{
	view_->filledframe(file_dlg_x, file_dlg_y, file_dlg_dx, 5);
	char ln[64];
	static const char *titles[] = { " Read from binary file   ",
									" Write to binary file    ",
									" Disasm to text file     " };
	view_->tprint(file_dlg_x, file_dlg_y, titles[mode], frm_header);
	view_->tprint(file_dlg_x + 1, file_dlg_y + 2, "file:", fframe_inside);
	sprintf(ln, (mode != FDM_LOAD) ? "start: %04X end: %04X" : "start: %04X", addr, end);
	view_->tprint(file_dlg_x + 1, file_dlg_y + 3, ln, fframe_inside);
	strcpy(str, fname);
	for (;;)
	{
		if (!view_->inputhex(file_dlg_x + 7, file_dlg_y + 2, 16, false))
			return 0;
		if (mode != FDM_LOAD)
			break;
		if (GetFileAttributes(str) != INVALID_FILE_ATTRIBUTES)
			break;
	}
	strcpy(fname, str);
	sprintf(ln, "%-16s", fname);
	view_->fillattr(file_dlg_x + 7, file_dlg_y + 2, 16);
	const unsigned a1 = view_->input4(file_dlg_x + 8, file_dlg_y + 3, addr);
	if (a1 == UINT_MAX)
		return 0;
	addr = a1;
	view_->fillattr(file_dlg_x + 8, file_dlg_y + 3, 4);
	if (mode == FDM_LOAD)
		return 1;
	for (;;)
	{
		const unsigned e1 = view_->input4(file_dlg_x + 18, file_dlg_y + 3, end);
		if (e1 == UINT_MAX)
			return 0;
		if (e1 < addr)
			continue;
		end = e1;
		return 1;
	}
}

auto DebugCore::write_mem(u8* memdata) const -> void
{
	auto& cpu = TCpuMgr::get_cpu();
	auto ptr = memdata;
	for (auto a1 = addr; a1 <= end; a1++)
		*cpu.DirectMem(a1) = *ptr++;
}

auto DebugCore::read_mem(u8* memdata) const -> void
{
	auto& cpu = TCpuMgr::get_cpu();
	auto ptr = memdata;
	for (auto a1 = addr; a1 <= end; a1++)
		*ptr++ = cpu.DirectRm(a1);
}

auto DebugCore::rw_select_drive() -> char
{
	view_->tprint(file_dlg_x + 1, file_dlg_y + 2, "drive:", fframe_inside);
	for (;;) {
		*reinterpret_cast<unsigned*>(str) = 'A' + rw_drive;
		if (!view_->inputhex(file_dlg_x + 8, file_dlg_y + 2, 1, true)) return 0;
		view_->fillattr(file_dlg_x + 8, file_dlg_y + 2, 1);
		const unsigned disk = *str - 'A';
		if (disk > 3) continue;
		if (!comp.wd.fdd[disk].rawdata) continue;
		rw_drive = disk; return 1;
	}
}

auto DebugCore::debug(Z80* cpu) -> void
{
	sound_stop();
	temp.mon_scale = temp.scale;
	temp.scale = 1;
	needclr = 1;
	dbgbreak = 1;
	flip();
	const auto oldrflags = temp.rflags;
	temp.rflags = RF_MONITOR;
	//set_video();
	ShowWindow(wnd_, SW_SHOW);

	TCpuMgr::set_current_cpu(cpu->GetIdx());
	auto prevcpu = &TCpuMgr::prev_cpu(cpu->GetIdx());
	cpu->trace_curs = cpu->pc;
	cpu->dbg_stopsp = cpu->dbg_stophere = -1;
	cpu->dbg_loop_r1 = 0, cpu->dbg_loop_r2 = 0xFFFF;
	mousepos = 0;

	while (dbgbreak) // debugger event loop
	{
		if (trace_->trace_labels)
			mon_labels.notify_user_labels();

		cpu = &TCpuMgr::get_cpu();
		prevcpu = &TCpuMgr::prev_cpu(cpu->GetIdx());
	repaint_dbg:
		cpu->trace_top &= 0xFFFF;
		cpu->trace_curs &= 0xFFFF;

		debugscr();
		if (cpu->trace_curs < cpu->trace_top || cpu->trace_curs >= cpu->trpc[trace_size] || trace_->asmii == UINT_MAX)
		{
			cpu->trace_top = cpu->trace_curs;
			debugscr();
		}

		view_->flip();

	sleep:
		while (!dispatch(nullptr))
		{
			if (mousepos)
				handle_mouse();
			if (needclr)
			{
				needclr--;
				goto repaint_dbg;
			}
			if (!dbgbreak)
				goto leave_dbg;	/* ugh... too much gotos... */
			Sleep(20);
		}

		if (activedbg == dbgwnd::regs && dispatch_more(ActionType::reg) > 0) continue;
		if (activedbg == dbgwnd::trace && dispatch_more(ActionType::trace) > 0) continue;
		if (activedbg == dbgwnd::mem && dispatch_more(ActionType::memory) > 0) continue;
		if (activedbg == dbgwnd::banks && dispatch_more(ActionType::banks) > 0) continue;
		if (activedbg == dbgwnd::regs &&  regs_->dispatch()) continue;
		if (activedbg == dbgwnd::trace && trace_->dispatch_trace()) continue;
		if (activedbg == dbgwnd::mem && mem_->dispatch()) continue;
		if (activedbg == dbgwnd::banks && banks_->dispatch()) continue;
		if (needclr)
		{
			needclr--;
			continue;
		}
		goto sleep;
	}

leave_dbg:
	*prevcpu = TZ80State(*cpu);
	//   CpuMgr.CopyToPrev();
	cpu->SetLastT();
	temp.scale = temp.mon_scale;
	//temp.rflags = RF_GDI; // facepalm.jpg
	temp.rflags = oldrflags;
	//apply_video();
	ShowWindow(wnd_, SW_HIDE);
	sound_play();
	input.nokb = 20;
}

auto DebugCore::debug_events(Z80* cpu) -> void
{
	const auto pc = cpu->pc & 0xFFFF;
	const auto membit = cpu->membits + pc;
	*membit |= MEMBITS_X;
	cpu->dbgbreak |= (*membit & MEMBITS_BPX);
	dbgbreak |= (*membit & MEMBITS_BPX);

	if (pc == cpu->dbg_stophere)
	{
		cpu->dbgbreak = 1;
		dbgbreak = 1;
	}

	if ((cpu->sp & 0xFFFF) == cpu->dbg_stopsp)
	{
		if (pc > cpu->dbg_stophere && pc < cpu->dbg_stophere + 0x100)
		{
			cpu->dbgbreak = 1;
			dbgbreak = 1;
		}
		if (pc < cpu->dbg_loop_r1 || pc > cpu->dbg_loop_r2)
		{
			cpu->dbgbreak = 1;
			dbgbreak = 1;
		}
	}

	debug_cond_check(cpu);
	brk_port_in = brk_port_out = -1; // reset only when breakpoints active
	brk_mem_rd = brk_mem_wr = -1;	// reset only when breakpoints active

	if (cpu->dbgbreak)
		get_instance()->debug(cpu);
}

auto DebugCore::handle_mouse() -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	const unsigned mx = ((mousepos & 0xFFFF) - temp.gx) / 8;
	const unsigned my = (((mousepos >> 16) & 0x7FFF) - temp.gy) / 16;

	if (my >= trace_y && my < trace_y + trace_size && mx >= trace_x && mx < trace_x + 32)
	{
		needclr++; activedbg = dbgwnd::trace;
		cpu.trace_curs = cpu.trpc[my - trace_y];
		if (mx - trace_x < trace_->cs[1][0]) cpu.trace_mode = 0;
		else if (mx - trace_x < trace_->cs[2][0]) cpu.trace_mode = 1;
		else cpu.trace_mode = 2;
	}
	if (my >= mem_y && my < mem_y + mem_size && mx >= mem_x && mx < mem_x + 37)
	{
		needclr++; activedbg = dbgwnd::mem;
		const auto dx = mx - mem_x;
		if (mem_->mem_dump)
		{
			if (dx >= 5)
				cpu.mem_curs = cpu.mem_top + (dx - 5) + (my - mem_y) * 32;
		}
		else
		{
			const auto mem_se = (dx - 5) % 3;
			if (dx >= 29) cpu.mem_curs = cpu.mem_top + (dx - 29) + (my - mem_y) * 8, mem_->mem_ascii = 1;
			if (dx >= 5 && mem_se != 2 && dx < 29)
				cpu.mem_curs = cpu.mem_top + (dx - 5) / 3 + (my - mem_y) * 8,
				cpu.mem_second = mem_se, mem_->mem_ascii = 0;
		}
	}
	if (mx >= regs_x && my >= regs_y && mx < regs_x + 32 && my < regs_y + 4) {
		needclr++; activedbg = dbgwnd::regs;
		for (unsigned i = 0; i < regs_layout.size(); i++) {
			unsigned delta = 1;
			if (regs_layout[i].width == 16) delta = 4;
			if (regs_layout[i].width == 8) delta = 2;
			if (my - regs_y == regs_layout[i].y && mx - regs_x - regs_layout[i].x < delta) {
				regs_->regs_curs = i;
			}
		}
	}
	if (mx >= banks_x && my >= banks_y + 1 && mx < banks_x + 7 && my < banks_y + 5) {
		needclr++;
		activedbg = dbgwnd::banks;
		banks_->selbank = my - (banks_y + 1);
		banks_->showbank = true;
	}
	else banks_->showbank = false;

	if (mousepos & 0x80000000) { // right-click
		enum { idm_bpx = 1, idm_some_other };
		const auto menu = CreatePopupMenu();
		if (activedbg == dbgwnd::trace) {
			AppendMenu(menu, MF_STRING, idm_bpx, "breakpoint");
		}
		else {
			AppendMenu(menu, MF_STRING, 0, "I don't know");
			AppendMenu(menu, MF_STRING, 0, "what to place");
			AppendMenu(menu, MF_STRING, 0, "to menu, so");
			AppendMenu(menu, MF_STRING, 0, "No Stuff Here");
		}
		POINT globalpos; GetCursorPos(&globalpos);
		const auto cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
			globalpos.x,
			globalpos.y, 0, wnd, nullptr);
		DestroyMenu(menu);
		if (cmd == idm_bpx) ActionManager::invoke(ActionType::trace, "bpx");
	}
	mousepos = 0;
}

auto DebugCore::debug_cond_check(Z80* cpu) -> void
{
	if (cpu->cbpn)
	{
		cpu->r_low = (cpu->r_low & 0x7F) + cpu->r_hi;
		for (unsigned i = 0; i < cpu->cbpn; i++)
		{
			if (calc(cpu, cpu->cbp[i]))
			{
				cpu->dbgbreak |= 1;
				dbgbreak |= 1;
			}
		}
	}
}

auto DebugCore::get_instance() -> DebugCore*
{
	if (instance_ == nullptr)
	{
		instance_ = new DebugCore();
		return instance_;
	}

	return instance_;
}

auto DebugCore::get_view() -> DebugView*
{
	return get_instance()->view_;
}

auto DebugCore::get_dialogs() -> Dialogs*
{
	return get_instance()->dialogs_;
}

auto DebugCore::get_trace() -> TraceView*
{
	return get_instance()->trace_;
}

auto DebugCore::isbrk(const Z80& cpu) -> u8
{
#ifndef MOD_DEBUGCORE
	return 0;
#else

#ifdef MOD_MEMBAND_LED
	if (conf.led.memband & 0x80000000)
		return 1;
#endif

	if (conf.mem_model == MM_PROFSCORP)
		return 1; // breakpoint on read ROM switches ROM bank

#ifdef MOD_MONITOR
	if (cpu.cbpn)
		return 1;
	u8 res = 0;
	for (auto i = 0; i < 0x10000; i++)
		res |= cpu.membits[i];
	return (res & (MEMBITS_BPR | MEMBITS_BPW | MEMBITS_BPX));
#endif

#endif
}

auto DebugCore::init_bpx(char* file) -> void
{
	addpath(bpx_file_name, file ? file : "bpx.ini");
	const auto bpx_file = fopen(bpx_file_name, "rt");
	if (!bpx_file) return;
	if (file)
	{
		color(CONSCLR_DEFAULT); printf("bpx: ");
		color(CONSCLR_INFO);    printf("%s\n", bpx_file_name);
	}

	char line[100];
	while (!feof(bpx_file))
	{
		fgets(line, sizeof(line), bpx_file);
		line[sizeof(line) - 1] = 0;
		char type = -1;
		auto start = -1, end = -1, cpu_idx = -1;
		const auto n = sscanf(line, "%c%1d=%i-%i", &type, &cpu_idx, &start, &end);
		if (n < 3 || cpu_idx < 0 || cpu_idx >= int(TCpuMgr::get_count()) || start < 0)
			continue;

		if (end < 0)
			end = start;

		unsigned mask = 0;
		switch (type)
		{
		case 'r': mask |= MEMBITS_BPR; break;
		case 'w': mask |= MEMBITS_BPW; break;
		case 'x': mask |= MEMBITS_BPX; break;
		default: continue;
		}

		auto& cpu = TCpuMgr::get_cpu(cpu_idx);
		for (auto i = unsigned(start); i <= unsigned(end); i++)
			cpu.membits[i] |= mask;
		cpu.dbgchk = isbrk(cpu);
	}
	fclose(bpx_file);
	}

auto DebugCore::done_bpx() -> void
{
	const auto bpx_file = fopen(bpx_file_name, "wt");
	if (!bpx_file) return;

	for (unsigned cpu_idx = 0; cpu_idx < TCpuMgr::get_count(); cpu_idx++)
	{
		auto& cpu = TCpuMgr::get_cpu(cpu_idx);

		for (auto i = 0; i < 3; i++)
		{
			for (unsigned start = 0; start < 0x10000; )
			{
				static const unsigned mask[] = { MEMBITS_BPR, MEMBITS_BPW, MEMBITS_BPX };
				if (!(cpu.membits[start] & mask[i]))
				{
					start++;
					continue;
				}
				const unsigned active = cpu.membits[start];
				unsigned end;
				for (end = start; end < 0xFFFF && !((active ^ cpu.membits[end + 1]) & mask[i]); end++) {}

				static const char type[] = { 'r', 'w', 'x' };
				if (active & mask[i])
				{
					if (start == end)
						fprintf(bpx_file, "%c%1d=0x%04X\n", type[i], cpu_idx, start);
					else
						fprintf(bpx_file, "%c%1d=0x%04X-0x%04X\n", type[i], cpu_idx, start, end);
				}

				start = end + 1;
			}
		}
	}

	fclose(bpx_file);
}

auto DebugCore::debugscr() const -> void
{
	view_->clear_canvas();

	regs_->render();
	trace_->show_trace();
	mem_->render();
	watch_->render();
	stack_->render();
	ay_->render();
	banks_->render();
	ports_->render();
	dos_->render();
	tsconf_->render();

	time_->render();
}

auto DebugCore::correct_exit() -> void
{
	sound_stop();
	if (!done_fdd(true))
		return;

	DeleteCriticalSection(&tsu_toggle_cr);

	nowait = 1;
	normal_exit = true;
	exit();
}

