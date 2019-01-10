#include "std.h"
#include "mem.h"
#include "debugger/consts.h"
#include "util.h"
#include "vars.h"
#include "wd93crc.h"
#include "debugger/core.h"
#include "debugger/libs/cpu_manager.h"

char str[80];

auto MemView::findsector(unsigned addr) -> void
{
	for (sector_offset = sector = 0; sector < edited_track.s; sector++, sector_offset += edited_track.hdr[sector].datlen)
		if (addr >= sector_offset && addr < sector_offset + edited_track.hdr[sector].datlen)
			return;

	errexit("internal disk editor error");
}

auto MemView::editam(unsigned addr) -> u8*
{
	auto& cpu = TCpuMgr::get_cpu();

	if (editor == ed_cmos)
		return &cmos[addr & (sizeof(cmos) - 1)];

	if (editor == ed_nvram)
		return &nvram[addr & (sizeof(nvram) - 1)];

	if (editor == ed_mem)
		return cpu.DirectMem(addr);

	if (!edited_track.trkd)
		return nullptr;

	if (editor == ed_phys)
		return edited_track.trkd + addr;

	// editor == ED_LOG
	findsector(addr); return edited_track.hdr[sector].data + addr - sector_offset;
}

auto MemView::editwm(unsigned addr, u8 byte) -> void
{
	if (editrm(addr) == byte)
		return;

	const auto ptr = editam(addr);
	if (!ptr)
		return;

	*ptr = byte;
	if (editor == ed_mem || editor == ed_cmos || editor == ed_nvram)
		return;

	if (editor == ed_phys)
	{
		comp.wd.fdd[mem_disk].optype |= 2; return;
	}
	comp.wd.fdd[mem_disk].optype |= 1;
	// recalc sector checksum
	findsector(addr);
	*reinterpret_cast<u16*>(edited_track.hdr[sector].data + edited_track.hdr[sector].datlen) =
		wd93_crc(edited_track.hdr[sector].data - 1, edited_track.hdr[sector].datlen + 1);
}

auto MemView::memadr(unsigned addr) const -> unsigned
{
	if (editor == ed_mem)
		return (addr & 0xFFFF);

	if (editor == ed_cmos)
		return (addr & (sizeof(cmos) - 1));

	if (editor == ed_nvram)
		return (addr & (sizeof(nvram) - 1));

	// else if (editor == ED_PHYS || editor == ED_LOG)
	if (!mem_max)
		return 0;

	while (int(addr) < 0)
		addr += mem_max;
	while (addr >= mem_max)
		addr -= mem_max;
	return addr;
}

auto MemView::editrm(unsigned addr) -> u8
{
	const auto ptr = editam(addr);
	return ptr ? *ptr : 0;
}

MemView::MemView(DebugCore& core, DebugView& view) : view_(view), core_(core)
{
}

auto MemView::mleft() const -> void
{
	if (!mem_max)
		return;

	auto& cpu = TCpuMgr::get_cpu();
	if (mem_ascii || !cpu.mem_second)
		cpu.mem_curs--;
	if (!mem_ascii)
		cpu.mem_second ^= 1;
}

auto MemView::mright() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	if (!mem_max)
		return;

	if (mem_ascii || cpu.mem_second)
		cpu.mem_curs++;
	if (!mem_ascii)
		cpu.mem_second ^= 1;
	if (((cpu.mem_curs - cpu.mem_top + mem_max) % mem_max) / mem_sz >= mem_size)
		cpu.mem_top += mem_sz;
}

auto MemView::mup() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	if (mem_max)
		cpu.mem_curs -= mem_sz;
}

auto MemView::mdown() const -> void
{
	if (!mem_max)
		return;

	auto& cpu = TCpuMgr::get_cpu();
	cpu.mem_curs += mem_sz;
	if (((cpu.mem_curs - cpu.mem_top + mem_max) % mem_max) / mem_sz >= mem_size)
		cpu.mem_top += mem_sz;
}

auto MemView::mpgdn() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	if (mem_max)
		cpu.mem_curs += mem_size * mem_sz, cpu.mem_top += mem_size * mem_sz;
}

auto MemView::mpgup() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	if (mem_max)
		cpu.mem_curs -= mem_size * mem_sz, cpu.mem_top -= mem_size * mem_sz;
}

auto MemView::mswitch() -> void
{
	mem_ascii ^= 1;
}

auto MemView::mstl() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	if (mem_max)
		cpu.mem_curs &= ~(mem_sz - 1), cpu.mem_second = 0;
}

auto MemView::mendl() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	if (mem_max)
		cpu.mem_curs |= (mem_sz - 1), cpu.mem_second = 1;
}

auto MemView::mtext() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();
	const unsigned rs = DebugCore::get_dialogs()->find1dlg(cpu.mem_curs);

	if (rs != UINT_MAX)
		cpu.mem_curs = rs;
}

auto MemView::mcode() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();
	const auto rs = DebugCore::get_dialogs()->find2dlg(cpu.mem_curs);

	if (rs != UINT_MAX)
		cpu.mem_curs = rs;
}

auto MemView::mgoto() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();
	const unsigned v = view_.input4(mem_x, mem_y, cpu.mem_top);

	if (v != UINT_MAX)
		cpu.mem_top = (v & ~(mem_sz - 1)), cpu.mem_curs = v;
}

auto MemView::mmodemem() -> void
{
	editor = ed_mem;
}

auto MemView::mmodephys() -> void
{
	editor = ed_phys;
}

auto MemView::mmodelog() -> void
{
	editor = ed_log;
}

auto MemView::mdiskgo() -> void
{
	Z80 &cpu = TCpuMgr::get_cpu();

	if (editor == ed_mem)
		return;

	for (;; )
	{
		*reinterpret_cast<unsigned*>(str) = mem_disk + 'A';
		if (!view_.inputhex(mem_x + 5, mem_y - 1, 1, true))
			return;

		if (*str >= 'A' && *str <= 'D')
			break;
	}
	mem_disk = *str - 'A'; show_mem();
	unsigned x = view_.input2(mem_x + 12, mem_y - 1, mem_track);
	if (x == UINT_MAX)
		return;

	mem_track = x;
	if (editor == ed_phys)
		return;

	show_mem();
	// enter sector
	for (;; )
	{
		findsector(cpu.mem_curs); x = view_.input2(mem_x + 20, mem_y - 1, sector);
		if (x == UINT_MAX)
			return;

		if (x < edited_track.s)
			break;
	}
	for (cpu.mem_curs = 0; x; x--)
		cpu.mem_curs += edited_track.hdr[x - 1].datlen;
}

auto MemView::mpc() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	cpu.mem_curs = cpu.pc;
}

auto MemView::msp() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	cpu.mem_curs = cpu.sp;
}

auto MemView::mbc() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	cpu.mem_curs = cpu.bc;
}

auto MemView::mde() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	cpu.mem_curs = cpu.de;
}

auto MemView::mhl() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	cpu.mem_curs = cpu.hl;
}

auto MemView::mix() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	cpu.mem_curs = cpu.ix;
}

auto MemView::miy() const -> void
{
	auto& cpu = TCpuMgr::get_cpu();

	cpu.mem_curs = cpu.iy;
}

auto MemView::show_mem() -> void
{
	auto& cpu = TCpuMgr::get_cpu();
	char     line[debug_text_width]; unsigned ii;
	unsigned cursor_found = 0;

	if (mem_dump)
		mem_ascii = 1, mem_sz = 32;
	else
		mem_sz = 8;

	if (editor == ed_log || editor == ed_phys)
	{
		edited_track.clear();
		edited_track.seek(comp.wd.fdd + mem_disk, mem_track / 2, mem_track & 1, (editor == ed_log) ? LOAD_SECTORS : JUST_SEEK);
		if (!edited_track.trkd)   // no track
		{
			for (ii = 0; ii < mem_size; ii++)
			{
				sprintf(line, (ii == mem_size / 2) ?
					"          track not found            " :
					"                                     ");
				view_.tprint(mem_x, mem_y + ii, line, (core_.activedbg == dbgwnd::mem) ? w_sel : w_norm);
			}
			mem_max = 0;
			goto title;
		}
		mem_max = edited_track.trklen;
		if (editor == ed_log)
			for (mem_max = ii = 0; ii < edited_track.s; ii++)
				mem_max += edited_track.hdr[ii].datlen;
	}
	else if (editor == ed_mem)
		mem_max = 0x10000;
	else if (editor == ed_cmos)
		mem_max = sizeof(cmos);
	else if (editor == ed_nvram)
		mem_max = sizeof(nvram);

redraw:

	cpu.mem_curs = memadr(cpu.mem_curs);
	cpu.mem_top = memadr(cpu.mem_top);
	for (ii = 0; ii < mem_size; ii++)
	{
		auto ptr = memadr(cpu.mem_top + ii * mem_sz);
		sprintf(line, "%04X ", ptr);
		unsigned cx = 0;
		if (!mem_dump)
		{  // 0000 11 22 33 44 55 66 77 88 abcdefgh
			for (unsigned dx = 0; dx < 8; dx++)
			{
				if (ptr == cpu.mem_curs)
					cx = (mem_ascii) ? dx + 29 : dx * 3 + 5 + cpu.mem_second;
				const auto c = editrm(ptr);
				ptr = memadr(ptr + 1);
				sprintf(line + 5 + 3 * dx, "%02X", c); line[7 + 3 * dx] = ' ';
				line[29 + dx] = c ? c : '.';
			}
		}
		else
		{  // 0000 0123456789ABCDEF0123456789ABCDEF
			for (unsigned dx = 0; dx < 32; dx++)
			{
				if (ptr == cpu.mem_curs)
					cx = dx + 5;
				const auto c = editrm(ptr);
				ptr = memadr(ptr + 1);
				line[dx + 5] = c ? c : '.';
			}
		}

		line[37] = 0;
		view_.tprint(mem_x, mem_y + ii, line, (core_.activedbg == dbgwnd::mem) ? w_sel : w_norm);
		cursor_found |= cx;
		if (cx && (core_.activedbg == dbgwnd::mem))
			txtscr[(mem_y + ii) * debug_text_width + mem_x + cx + debug_text_width * debug_text_height] = w_curs;
	}

	if (!cursor_found)
	{
		cursor_found = 1; cpu.mem_top = cpu.mem_curs & ~(mem_sz - 1); goto redraw;
	}
title:
	const char *mem_name = nullptr;
	if (editor == ed_mem)
		mem_name = "memory";
	else if (editor == ed_cmos)
		mem_name = "cmos";
	else if (editor == ed_nvram)
		mem_name = "nvram";

	if (editor == ed_mem || editor == ed_cmos || editor == ed_nvram)
	{
		sprintf(line, "%s: %04X gsdma: %06X", mem_name, cpu.mem_curs & 0xFFFF, temp.gsdmaaddr);
	}
	else if (editor == ed_phys)
		sprintf(line, "disk %c, trk %02X, offs %04X", mem_disk + 'A', mem_track, cpu.mem_curs);
	else
	{ // ED_LOG
		if (mem_max)
			findsector(cpu.mem_curs);
		sprintf(line, "disk %c, trk %02X, sec %02X[%02X], offs %04X",
			mem_disk + 'A', mem_track, sector, edited_track.hdr[sector].n,
			cpu.mem_curs - sector_offset);
	}
	view_.tprint(mem_x, mem_y - 1, line, w_title);
	view_.add_frame(mem_x, mem_y, 37, mem_size, FRAME);
}

auto MemView::dispatch_mem() -> char
{
	auto& cpu = TCpuMgr::get_cpu();

	if (!mem_max)
		return 0;

	if (mem_ascii)
	{
		u8  kbd[256];
		GetKeyboardState(kbd);
		u16 k;
		if (ToAscii(input.lastkey, 0, kbd, &k, 0) != 1)
			return 0;

		k &= 0xFF;
		if (k < 0x20 || k >= 0x80)
			return 0;

		editwm(cpu.mem_curs, u8(k));
		mright();
		return 1;
	}
	else
	{
		if ((input.lastkey >= '0' && input.lastkey <= '9') || (input.lastkey >= 'A' && input.lastkey <= 'F'))
		{
			const u8 k = (input.lastkey >= 'A') ? input.lastkey - 'A' + 10 : input.lastkey - '0';
			const u8 c = editrm(cpu.mem_curs);
			if (cpu.mem_second)
				editwm(cpu.mem_curs, (c & 0xF0) | k);
			else
				editwm(cpu.mem_curs, (c & 0x0F) | (k << 4));
			mright();
			return 1;
		}
	}
	return 0;
}