/*-
 * Copyright (c) 2009,2010 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <ar.h>
#include <ctype.h>
#include <cgcdwarf.h>
#include <err.h>
#include <fcntl.h>
#include <gcgcef.h>
#include <getopt.h>
#include <inttypes.h>
#include <libcgcdwarf.h>
#include <libcgcef.h>
//#include <libcgceftc.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

//#include "_cgceftc.h"

//CGCEFTC_VCSID("$Id$");

/*
 * readcgcef(1) options.
 */
#define	RE_AA	0x00000001
#define	RE_C	0x00000002
#define	RE_DD	0x00000004
#define	RE_D	0x00000008
#define	RE_G	0x00000010
#define	RE_H	0x00000020
#define	RE_II	0x00000040
#define	RE_I	0x00000080
#define	RE_L	0x00000100
#define	RE_NN	0x00000200
#define	RE_N	0x00000400
#define	RE_P	0x00000800
#define	RE_R	0x00001000
#define	RE_SS	0x00002000
#define	RE_S	0x00004000
#define	RE_T	0x00008000
#define	RE_U	0x00010000
#define	RE_VV	0x00020000
#define	RE_WW	0x00040000
#define	RE_W	0x00080000
#define	RE_X	0x00100000

/*
 * dwarf dump options.
 */
#define	DW_A	0x00000001
#define	DW_FF	0x00000002
#define	DW_F	0x00000004
#define	DW_I	0x00000008
#define	DW_LL	0x00000010
#define	DW_L	0x00000020
#define	DW_M	0x00000040
#define	DW_O	0x00000080
#define	DW_P	0x00000100
#define	DW_RR	0x00000200
#define	DW_R	0x00000400
#define	DW_S	0x00000800

#define	DW_DEFAULT_OPTIONS (DW_A | DW_F | DW_I | DW_L | DW_O | DW_P | \
	    DW_R | DW_RR | DW_S)

/*
 * readcgcef(1) run control flags.
 */
#define	DISPLAY_FILENAME	0x0001

#ifndef CGCEFTC_GETPROGNAME
/* KLUDGE ALERT */
# define CGCEFTC_GETPROGNAME()	"dwarf"
#endif /* CGCEFTC_GETPROGNAME */

/*
 * Internal data structure for sections.
 */
struct section {
	const char	*name;		/* section name */
	CGCEf_Scn		*scn;		/* section scn */
	uint64_t	 off;		/* section offset */
	uint64_t	 sz;		/* section size */
	uint64_t	 entsize;	/* section entsize */
	uint64_t	 align;		/* section alignment */
	uint64_t	 type;		/* section type */
	uint64_t	 flags;		/* section flags */
	uint64_t	 addr;		/* section virtual addr */
	uint32_t	 link;		/* section link ndx */
	uint32_t	 info;		/* section info ndx */
};

struct dumpop {
	union {
		size_t si;		/* section index */
		const char *sn;		/* section name */
	} u;
	enum {
		DUMP_BY_INDEX = 0,
		DUMP_BY_NAME
	} type;				/* dump type */
#define HEX_DUMP	0x0001
#define STR_DUMP	0x0002
	int op;				/* dump operation */
	STAILQ_ENTRY(dumpop) dumpop_list;
};

struct symver {
	const char *name;
	int type;
};

/*
 * Structure encapsulates the global data for readcgcef(1).
 */
struct readcgcef {
	const char	 *filename;	/* current processing file. */
	int		  options;	/* command line options. */
	int		  flags;	/* run control flags. */
	int		  dop;		/* dwarf dump options. */
	CGCEf		 *cgcef;	/* underlying CGC descriptor. */
	CGCEf		 *ar;		/* archive CGC descriptor. */
	Dwarf_Debug	  dbg;		/* DWARF handle. */
	GCGCEf_Ehdr	  ehdr;		/* ELF header. */
	int		  ec;		/* CGCEF class. */
	size_t		  shnum;	/* #sections. */
	struct section	 *vd_s;		/* Verdef section. */
	struct section	 *vn_s;		/* Verneed section. */
	struct section	 *vs_s;		/* Versym section. */
	uint16_t	 *vs;		/* Versym array. */
	int		  vs_sz;	/* Versym array size. */
	struct symver	 *ver;		/* Version array. */
	int		  ver_sz;	/* Size of version array. */
	struct section	 *sl;		/* list of sections. */
	STAILQ_HEAD(, dumpop) v_dumpop; /* list of dump ops. */
	uint64_t	(*dw_read)(CGCEf_Data *, uint64_t *, int);
	uint64_t	(*dw_decode)(uint8_t **, int);
};

enum options
{
	OPTION_DEBUG_DUMP
};

static struct option longopts[] = {
	{"all", no_argument, NULL, 'a'},
	{"arch-specific", no_argument, NULL, 'A'},
	{"archive-index", no_argument, NULL, 'c'},
	{"debug-dump", optional_argument, NULL, OPTION_DEBUG_DUMP},
	{"dynamic", no_argument, NULL, 'd'},
	{"file-header", no_argument, NULL, 'h'},
	{"full-section-name", no_argument, NULL, 'N'},
	{"headers", no_argument, NULL, 'e'},
	{"help", no_argument, 0, 'H'},
	{"hex-dump", required_argument, NULL, 'x'},
	{"histogram", no_argument, NULL, 'I'},
	{"notes", no_argument, NULL, 'n'},
	{"program-headers", no_argument, NULL, 'l'},
	{"relocs", no_argument, NULL, 'r'},
	{"sections", no_argument, NULL, 'S'},
	{"section-headers", no_argument, NULL, 'S'},
	{"section-groups", no_argument, NULL, 'g'},
	{"section-details", no_argument, NULL, 't'},
	{"segments", no_argument, NULL, 'l'},
	{"string-dump", required_argument, NULL, 'p'},
	{"symbols", no_argument, NULL, 's'},
	{"syms", no_argument, NULL, 's'},
	{"unwind", no_argument, NULL, 'u'},
	{"use-dynamic", no_argument, NULL, 'D'},
	{"version-info", no_argument, 0, 'V'},
	{"version", no_argument, 0, 'v'},
	{"wide", no_argument, 0, 'W'},
	{NULL, 0, NULL, 0}
};

struct eflags_desc {
	uint64_t flag;
	const char *desc;
};

struct mips_option {
	uint64_t flag;
	const char *desc;
};

static void add_dumpop(struct readcgcef *re, size_t si, const char *sn, int op,
    int t);
static const char *aeabi_adv_simd_arch(uint64_t simd);
static const char *aeabi_align_needed(uint64_t an);
static const char *aeabi_align_preserved(uint64_t ap);
static const char *aeabi_arm_isa(uint64_t ai);
static const char *aeabi_cpu_arch(uint64_t arch);
static const char *aeabi_cpu_arch_profile(uint64_t pf);
static const char *aeabi_div(uint64_t du);
static const char *aeabi_enum_size(uint64_t es);
static const char *aeabi_fp_16bit_format(uint64_t fp16);
static const char *aeabi_fp_arch(uint64_t fp);
static const char *aeabi_fp_denormal(uint64_t fd);
static const char *aeabi_fp_exceptions(uint64_t fe);
static const char *aeabi_fp_hpext(uint64_t fh);
static const char *aeabi_fp_number_model(uint64_t fn);
static const char *aeabi_fp_optm_goal(uint64_t fog);
static const char *aeabi_fp_rounding(uint64_t fr);
static const char *aeabi_hardfp(uint64_t hfp);
static const char *aeabi_mpext(uint64_t mp);
static const char *aeabi_optm_goal(uint64_t og);
static const char *aeabi_pcs_config(uint64_t pcs);
static const char *aeabi_pcs_got(uint64_t got);
static const char *aeabi_pcs_r9(uint64_t r9);
static const char *aeabi_pcs_ro(uint64_t ro);
static const char *aeabi_pcs_rw(uint64_t rw);
static const char *aeabi_pcs_wchar_t(uint64_t wt);
static const char *aeabi_t2ee(uint64_t t2ee);
static const char *aeabi_thumb_isa(uint64_t ti);
static const char *aeabi_fp_user_exceptions(uint64_t fu);
static const char *aeabi_unaligned_access(uint64_t ua);
static const char *aeabi_vfp_args(uint64_t va);
static const char *aeabi_virtual(uint64_t vt);
static const char *aeabi_wmmx_arch(uint64_t wmmx);
static const char *aeabi_wmmx_args(uint64_t wa);
static const char *cgcef_class(unsigned int class);
static const char *cgcef_endian(unsigned int endian);
static const char *cgcef_machine(unsigned int mach);
static const char *cgcef_osabi(unsigned int abi);
static const char *cgcef_type(unsigned int type);
static const char *cgcef_ver(unsigned int ver);
static const char *dt_type(unsigned int mach, unsigned int dtype);
static void dump_ar(struct readcgcef *re, int);
static void dump_arm_attributes(struct readcgcef *re, uint8_t *p, uint8_t *pe);
//static void dump_attributes(struct readcgcef *re);
static uint8_t *dump_compatibility_tag(uint8_t *p);
static void dump_dwarf(struct readcgcef *re);
static void dump_eflags(struct readcgcef *re, uint64_t e_flags);
static void dump_cgcef(struct readcgcef *re);
static void dump_dyn_val(struct readcgcef *re, GCGCEf_Dyn *dyn, uint32_t stab);
static void dump_dynamic(struct readcgcef *re);
//static void dump_liblist(struct readcgcef *re);
//static void dump_mips_attributes(struct readcgcef *re, uint8_t *p, uint8_t *pe);
//static void dump_mips_odk_reginfo(struct readcgcef *re, uint8_t *p, size_t sz);
//static void dump_mips_options(struct readcgcef *re, struct section *s);
//static void dump_mips_option_flags(const char *name, struct mips_option *opt,
//    uint64_t info);
//static void dump_mips_reginfo(struct readcgcef *re, struct section *s);
//static void dump_mips_specific_info(struct readcgcef *re);
#if 0
static void dump_notes(struct readcgcef *re);
static void dump_notes_content(struct readcgcef *re, const char *buf, size_t sz,
    off_t off);
#endif
static void dump_svr4_hash(struct section *s);
static void dump_svr4_hash64(struct readcgcef *re, struct section *s);
static void dump_gnu_hash(struct readcgcef *re, struct section *s);
static void dump_hash(struct readcgcef *re);
static void dump_phdr(struct readcgcef *re);
static void dump_ppc_attributes(uint8_t *p, uint8_t *pe);
static void dump_symtab(struct readcgcef *re, int i);
static void dump_symtabs(struct readcgcef *re);
static uint8_t *dump_unknown_tag(uint64_t tag, uint8_t *p);
static void dump_ver(struct readcgcef *re);
static void dump_verdef(struct readcgcef *re, int dump);
static void dump_verneed(struct readcgcef *re, int dump);
static void dump_versym(struct readcgcef *re);
static struct dumpop *find_dumpop(struct readcgcef *re, size_t si, const char *sn,
    int op, int t);
static const char *get_string(struct readcgcef *re, int strtab, size_t off);
static const char *get_symbol_name(struct readcgcef *re, int symtab, int i);
static uint64_t get_symbol_value(struct readcgcef *re, int symtab, int i);
static void load_sections(struct readcgcef *re);
static const char *mips_abi_fp(uint64_t fp);
static const char *note_type(unsigned int osabi, unsigned int et,
    unsigned int nt);
static const char *option_kind(uint8_t kind);
static const char *phdr_type(unsigned int ptype);
static const char *ppc_abi_fp(uint64_t fp);
static const char *ppc_abi_vector(uint64_t vec);
static const char *r_type(unsigned int mach, unsigned int type);
static void readcgcef_usage(void);
static void readcgcef_version(void);
static void search_ver(struct readcgcef *re);
static const char *section_type(unsigned int mach, unsigned int stype);
static const char *st_bind(unsigned int sbind);
static const char *st_shndx(unsigned int shndx);
static const char *st_type(unsigned int stype);
static const char *st_vis(unsigned int svis);
static const char *top_tag(unsigned int tag);
static void unload_sections(struct readcgcef *re);
static uint64_t _read_lsb(CGCEf_Data *d, uint64_t *offsetp,
    int bytes_to_read);
static uint64_t _read_msb(CGCEf_Data *d, uint64_t *offsetp,
    int bytes_to_read);
static uint64_t _decode_lsb(uint8_t **data, int bytes_to_read);
static uint64_t _decode_msb(uint8_t **data, int bytes_to_read);
static int64_t _decode_sleb128(uint8_t **dp);
static uint64_t _decode_uleb128(uint8_t **dp);

static struct eflags_desc arm_eflags_desc[] = {
//	{EF_ARM_RELEXEC, "relocatable executable"},
//	{EF_ARM_HASENTRY, "has entry point"},
//	{EF_ARM_SYMSARESORTED, "sorted symbol tables"},
//	{EF_ARM_DYNSYMSUSESEGIDX, "dynamic symbols use segment index"},
//	{EF_ARM_MAPSYMSFIRST, "mapping symbols precede others"},
//	{EF_ARM_BE8, "BE8"},
//	{EF_ARM_LE8, "LE8"},
//	{EF_ARM_INTERWORK, "interworking enabled"},
//	{EF_ARM_APCS_26, "uses APCS/26"},
//	{EF_ARM_APCS_FLOAT, "uses APCS/float"},
//	{EF_ARM_PIC, "position independent"},
//	{EF_ARM_ALIGN8, "8 bit structure alignment"},
//	{EF_ARM_NEW_ABI, "uses new ABI"},
//	{EF_ARM_OLD_ABI, "uses old ABI"},
//	{EF_ARM_SOFT_FLOAT, "software FP"},
//	{EF_ARM_VFP_FLOAT, "VFP"},
//	{EF_ARM_MAVERICK_FLOAT, "Maverick FP"},
	{0, NULL}
};

static struct eflags_desc mips_eflags_desc[] = {
//	{EF_MIPS_NOREORDER, "noreorder"},
//	{EF_MIPS_PIC, "pic"},
//	{EF_MIPS_CPIC, "cpic"},
//	{EF_MIPS_UCODE, "ugen_reserved"},
//	{EF_MIPS_ABI2, "abi2"},
//	{EF_MIPS_OPTIONS_FIRST, "odk first"},
//	{EF_MIPS_ARCH_ASE_MDMX, "mdmx"},
//	{EF_MIPS_ARCH_ASE_M16, "mips16"},
	{0, NULL}
};

static struct eflags_desc powerpc_eflags_desc[] = {
//	{EF_PPC_EMB, "emb"},
//	{EF_PPC_RELOCATABLE, "relocatable"},
//	{EF_PPC_RELOCATABLE_LIB, "relocatable-lib"},
	{0, NULL}
};

static struct eflags_desc sparc_eflags_desc[] = {
//	{EF_SPARC_32PLUS, "v8+"},
//	{EF_SPARC_SUN_US1, "ultrasparcI"},
//	{EF_SPARC_HAL_R1, "halr1"},
//	{EF_SPARC_SUN_US3, "ultrasparcIII"},
	{0, NULL}
};

static const char *
cgcef_osabi(unsigned int abi)
{
	static char s_abi[32];

	switch(abi) {
	case CGCEFOSABI_CGCOS: return "CGC";
//	case CGCEFOSABI_SYSV: return "SYSV";
//	case CGCEFOSABI_HPUX: return "HPUS";
//	case CGCEFOSABI_NETBSD: return "NetBSD";
//	case CGCEFOSABI_GNU: return "GNU";
//	case CGCEFOSABI_HURD: return "HURD";
//	case CGCEFOSABI_86OPEN: return "86OPEN";
//	case CGCEFOSABI_SOLARIS: return "Solaris";
//	case CGCEFOSABI_AIX: return "AIX";
//	case CGCEFOSABI_IRIX: return "IRIX";
//	case CGCEFOSABI_FREEBSD: return "FreeBSD";
//	case CGCEFOSABI_TRU64: return "TRU64";
//	case CGCEFOSABI_MODESTO: return "MODESTO";
//	case CGCEFOSABI_OPENBSD: return "OpenBSD";
//	case CGCEFOSABI_OPENVMS: return "OpenVMS";
//	case CGCEFOSABI_NSK: return "NSK";
//	case CGCEFOSABI_ARM: return "ARM";
//	case CGCEFOSABI_STANDALONE: return "StandAlone";
	default:
		snprintf(s_abi, sizeof(s_abi), "<unknown: %#x>", abi);
		return (s_abi);
	}
};

static const char *
cgcef_machine(unsigned int mach)
{
	static char s_mach[32];

	switch (mach) {
	case EM_NONE: return "Unknown machine";
//	case EM_M32: return "AT&T WE32100";
//	case EM_SPARC: return "Sun SPARC";
	case EM_386: return "Intel i386";
//	case EM_68K: return "Motorola 68000";
//	case EM_88K: return "Motorola 88000";
//	case EM_860: return "Intel i860";
//	case EM_MIPS: return "MIPS R3000 Big-Endian only";
//	case EM_S370: return "IBM System/370";
//	case EM_MIPS_RS3_LE: return "MIPS R3000 Little-Endian";
//	case EM_PARISC: return "HP PA-RISC";
//	case EM_VPP500: return "Fujitsu VPP500";
//	case EM_SPARC32PLUS: return "SPARC v8plus";
//	case EM_960: return "Intel 80960";
//	case EM_PPC: return "PowerPC 32-bit";
//	case EM_PPC64: return "PowerPC 64-bit";
//	case EM_S390: return "IBM System/390";
//	case EM_V800: return "NEC V800";
//	case EM_FR20: return "Fujitsu FR20";
//	case EM_RH32: return "TRW RH-32";
//	case EM_RCE: return "Motorola RCE";
//	case EM_ARM: return "ARM";
//	case EM_SH: return "Hitachi SH";
//	case EM_SPARCV9: return "SPARC v9 64-bit";
//	case EM_TRICORE: return "Siemens TriCore embedded processor";
//	case EM_ARC: return "Argonaut RISC Core";
//	case EM_H8_300: return "Hitachi H8/300";
//	case EM_H8_300H: return "Hitachi H8/300H";
//	case EM_H8S: return "Hitachi H8S";
//	case EM_H8_500: return "Hitachi H8/500";
//	case EM_IA_64: return "Intel IA-64 Processor";
//	case EM_MIPS_X: return "Stanford MIPS-X";
//	case EM_COLDFIRE: return "Motorola ColdFire";
//	case EM_68HC12: return "Motorola M68HC12";
//	case EM_MMA: return "Fujitsu MMA";
//	case EM_PCP: return "Siemens PCP";
//	case EM_NCPU: return "Sony nCPU";
//	case EM_NDR1: return "Denso NDR1 microprocessor";
//	case EM_STARCORE: return "Motorola Star*Core processor";
//	case EM_ME16: return "Toyota ME16 processor";
//	case EM_ST100: return "STMicroelectronics ST100 processor";
//	case EM_TINYJ: return "Advanced Logic Corp. TinyJ processor";
	case EM_X86_64: return "Advanced Micro Devices x86-64";
//	case EM_PDSP: return "Sony DSP Processor";
//	case EM_FX66: return "Siemens FX66 microcontroller";
//	case EM_ST9PLUS: return "STMicroelectronics ST9+ 8/16 microcontroller";
//	case EM_ST7: return "STmicroelectronics ST7 8-bit microcontroller";
//	case EM_68HC16: return "Motorola MC68HC16 microcontroller";
//	case EM_68HC11: return "Motorola MC68HC11 microcontroller";
//	case EM_68HC08: return "Motorola MC68HC08 microcontroller";
//	case EM_68HC05: return "Motorola MC68HC05 microcontroller";
//	case EM_SVX: return "Silicon Graphics SVx";
//	case EM_ST19: return "STMicroelectronics ST19 8-bit mc";
//	case EM_VAX: return "Digital VAX";
//	case EM_CRIS: return "Axis Communications 32-bit embedded processor";
//	case EM_JAVELIN: return "Infineon Tech. 32bit embedded processor";
//	case EM_FIREPATH: return "Element 14 64-bit DSP Processor";
//	case EM_ZSP: return "LSI Logic 16-bit DSP Processor";
//	case EM_MMIX: return "Donald Knuth's educational 64-bit proc";
//	case EM_HUANY: return "Harvard University MI object files";
//	case EM_PRISM: return "SiTera Prism";
//	case EM_AVR: return "Atmel AVR 8-bit microcontroller";
//	case EM_FR30: return "Fujitsu FR30";
//	case EM_D10V: return "Mitsubishi D10V";
//	case EM_D30V: return "Mitsubishi D30V";
//	case EM_V850: return "NEC v850";
//	case EM_M32R: return "Mitsubishi M32R";
//	case EM_MN10300: return "Matsushita MN10300";
//	case EM_MN10200: return "Matsushita MN10200";
//	case EM_PJ: return "picoJava";
//	case EM_OPENRISC: return "OpenRISC 32-bit embedded processor";
//	case EM_ARC_A5: return "ARC Cores Tangent-A5";
//	case EM_XTENSA: return "Tensilica Xtensa Architecture";
//	case EM_VIDEOCORE: return "Alphamosaic VideoCore processor";
//	case EM_TMM_GPP: return "Thompson Multimedia General Purpose Processor";
//	case EM_NS32K: return "National Semiconductor 32000 series";
//	case EM_TPC: return "Tenor Network TPC processor";
//	case EM_SNP1K: return "Trebia SNP 1000 processor";
//	case EM_ST200: return "STMicroelectronics ST200 microcontroller";
//	case EM_IP2K: return "Ubicom IP2xxx microcontroller family";
//	case EM_MAX: return "MAX Processor";
//	case EM_CR: return "National Semiconductor CompactRISC microprocessor";
//	case EM_F2MC16: return "Fujitsu F2MC16";
//	case EM_MSP430: return "TI embedded microcontroller msp430";
//	case EM_BLACKFIN: return "Analog Devices Blackfin (DSP) processor";
//	case EM_SE_C33: return "S1C33 Family of Seiko Epson processors";
//	case EM_SEP: return "Sharp embedded microprocessor";
//	case EM_ARCA: return "Arca RISC Microprocessor";
//	case EM_UNICORE: return "Microprocessor series from PKU-Unity Ltd";
	default:
		snprintf(s_mach, sizeof(s_mach), "<unknown: %#x>", mach);
		return (s_mach);
	}

}

static const char *
cgcef_class(unsigned int class)
{
	static char s_class[32];

	switch (class) {
	case CGCEFCLASSNONE: return "none";
	case CGCEFCLASS32: return "CGCEF32";
	case CGCEFCLASS64: return "CGCEF64";
	default:
		snprintf(s_class, sizeof(s_class), "<unknown: %#x>", class);
		return (s_class);
	}
}

static const char *
cgcef_endian(unsigned int endian)
{
	static char s_endian[32];

	switch (endian) {
	case CGCEFDATANONE: return "none";
	case CGCEFDATA2LSB: return "2's complement, little endian";
	case CGCEFDATA2MSB: return "2's complement, big endian";
	default:
		snprintf(s_endian, sizeof(s_endian), "<unknown: %#x>", endian);
		return (s_endian);
	}
}

static const char *
cgcef_type(unsigned int type)
{
	static char s_type[32];

	switch (type) {
	case ET_NONE: return "NONE (None)";
	case ET_REL: return "REL (Relocatable file)";
	case ET_EXEC: return "EXEC (Executable file)";
	case ET_DYN: return "DYN (Shared object file)";
	case ET_CORE: return "CORE (Core file)";
	default:
		if (type >= ET_LOPROC)
			snprintf(s_type, sizeof(s_type), "<proc: %#x>", type);
		else if (type >= ET_LOOS && type <= ET_HIOS)
			snprintf(s_type, sizeof(s_type), "<os: %#x>", type);
		else
			snprintf(s_type, sizeof(s_type), "<unknown: %#x>",
			    type);
		return (s_type);
	}
}

static const char *
cgcef_ver(unsigned int ver)
{
	static char s_ver[32];

	switch (ver) {
	case EV_CURRENT: return "(current)";
	case EV_NONE: return "(none)";
	default:
		snprintf(s_ver, sizeof(s_ver), "<unknown: %#x>",
		    ver);
		return (s_ver);
	}
}

static const char *
phdr_type(unsigned int ptype)
{
	static char s_ptype[32];

	switch (ptype) {
	case PT_NULL: return "NULL";
	case PT_LOAD: return "LOAD";
	case PT_DYNAMIC: return "DYNAMIC(unsup)";
	case PT_INTERP: return "INTERP(unsup)";
	case PT_NOTE: return "NOTE(unsup)";
	case PT_SHLIB: return "SHLIB(unsup)";
	case PT_PHDR: return "PHDR";
	case PT_TLS: return "TLS(unsup)";
//	case PT_GNU_EH_FRAME: return "GNU_EH_FRAME(unsup)";
//	case PT_GNU_STACK: return "GNU_STACK(unsup)";
//	case PT_GNU_RELRO: return "GNU_RELRO(unsup)";
	case PT_CGCPOV2: return "CGC_PoV2";
	default:
		if (ptype >= PT_LOPROC && ptype <= PT_HIPROC)
			snprintf(s_ptype, sizeof(s_ptype), "LOPROC+%#x(unsup)",
			    ptype - PT_LOPROC);
		else if (ptype >= PT_LOOS && ptype <= PT_HIOS)
			snprintf(s_ptype, sizeof(s_ptype), "LOOS+%#x(unsup)",
			    ptype - PT_LOOS);
		else
			snprintf(s_ptype, sizeof(s_ptype), "<unsup: %#x>",
			    ptype);
		return (s_ptype);
	}
}

static const char *
section_type(unsigned int mach, unsigned int stype)
{
	static char s_stype[32];

#if 0
	if (stype >= SHT_LOPROC && stype <= SHT_HIPROC) {
		switch (mach) {
		case EM_X86_64:
			switch (stype) {
			case SHT_AMD64_UNWIND: return "AMD64_UNWIND";
			default:
				break;
			}
			break;
		case EM_MIPS:
		case EM_MIPS_RS3_LE:
			switch (stype) {
			case SHT_MIPS_LIBLIST: return "MIPS_LIBLIST";
			case SHT_MIPS_MSYM: return "MIPS_MSYM";
			case SHT_MIPS_CONFLICT: return "MIPS_CONFLICT";
			case SHT_MIPS_GPTAB: return "MIPS_GPTAB";
			case SHT_MIPS_UCODE: return "MIPS_UCODE";
			case SHT_MIPS_DEBUG: return "MIPS_DEBUG";
			case SHT_MIPS_REGINFO: return "MIPS_REGINFO";
			case SHT_MIPS_PACKAGE: return "MIPS_PACKAGE";
			case SHT_MIPS_PACKSYM: return "MIPS_PACKSYM";
			case SHT_MIPS_RELD: return "MIPS_RELD";
			case SHT_MIPS_IFACE: return "MIPS_IFACE";
			case SHT_MIPS_CONTENT: return "MIPS_CONTENT";
			case SHT_MIPS_OPTIONS: return "MIPS_OPTIONS";
			case SHT_MIPS_DELTASYM: return "MIPS_DELTASYM";
			case SHT_MIPS_DELTAINST: return "MIPS_DELTAINST";
			case SHT_MIPS_DELTACLASS: return "MIPS_DELTACLASS";
			case SHT_MIPS_DWARF: return "MIPS_DWARF";
			case SHT_MIPS_DELTADECL: return "MIPS_DELTADECL";
			case SHT_MIPS_SYMBOL_LIB: return "MIPS_SYMBOL_LIB";
			case SHT_MIPS_EVENTS: return "MIPS_EVENTS";
			case SHT_MIPS_TRANSLATE: return "MIPS_TRANSLATE";
			case SHT_MIPS_PIXIE: return "MIPS_PIXIE";
			case SHT_MIPS_XLATE: return "MIPS_XLATE";
			case SHT_MIPS_XLATE_DEBUG: return "MIPS_XLATE_DEBUG";
			case SHT_MIPS_WHIRL: return "MIPS_WHIRL";
			case SHT_MIPS_EH_REGION: return "MIPS_EH_REGION";
			case SHT_MIPS_XLATE_OLD: return "MIPS_XLATE_OLD";
			case SHT_MIPS_PDR_EXCEPTION: return "MIPS_PDR_EXCEPTION";
			default:
				break;
			}
			break;
		default:
			break;
		}

		snprintf(s_stype, sizeof(s_stype), "LOPROC+%#x",
		    stype - SHT_LOPROC);
		return (s_stype);
	}
#endif /* 0 */

	switch (stype) {
	case SHT_NULL: return "NULL";
	case SHT_PROGBITS: return "PROGBITS";
	case SHT_SYMTAB: return "SYMTAB";
	case SHT_STRTAB: return "STRTAB";
	case SHT_RELA: return "RELA";
	case SHT_HASH: return "HASH";
	case SHT_DYNAMIC: return "DYNAMIC";
	case SHT_NOTE: return "NOTE";
	case SHT_NOBITS: return "NOBITS";
	case SHT_REL: return "REL";
	case SHT_SHLIB: return "SHLIB";
	case SHT_DYNSYM: return "DYNSYM";
	case SHT_INIT_ARRAY: return "INIT_ARRAY";
	case SHT_FINI_ARRAY: return "FINI_ARRAY";
	case SHT_PREINIT_ARRAY: return "PREINIT_ARRAY";
	case SHT_GROUP: return "GROUP";
	case SHT_SYMTAB_SHNDX: return "SYMTAB_SHNDX";
//	case SHT_SUNW_dof: return "SUNW_dof";
//	case SHT_SUNW_cap: return "SUNW_cap";
//	case SHT_GNU_HASH: return "GNU_HASH";
//	case SHT_SUNW_ANNOTATE: return "SUNW_ANNOTATE";
//	case SHT_SUNW_DEBUGSTR: return "SUNW_DEBUGSTR";
//	case SHT_SUNW_DEBUG: return "SUNW_DEBUG";
//	case SHT_SUNW_move: return "SUNW_move";
//	case SHT_SUNW_COMDAT: return "SUNW_COMDAT";
//	case SHT_SUNW_syminfo: return "SUNW_syminfo";
//	case SHT_SUNW_verdef: return "SUNW_verdef";
//	case SHT_SUNW_verneed: return "SUNW_verneed";
//	case SHT_SUNW_versym: return "SUNW_versym";
	default:
		if (stype >= SHT_LOOS && stype <= SHT_HIOS)
			snprintf(s_stype, sizeof(s_stype), "LOOS+%#x",
			    stype - SHT_LOOS);
		else if (stype >= SHT_LOUSER)
			snprintf(s_stype, sizeof(s_stype), "LOUSER+%#x",
			    stype - SHT_LOUSER);
		else
			snprintf(s_stype, sizeof(s_stype), "<unknown: %#x>",
			    stype);
		return (s_stype);
	}
}

static const char *
dt_type(unsigned int mach, unsigned int dtype)
{
	static char s_dtype[32];

#if 0
	if (dtype >= DT_LOPROC && dtype <= DT_HIPROC) {
		switch (mach) {
		case EM_ARM:
			switch (dtype) {
			case DT_ARM_SYMTABSZ:
				return "ARM_SYMTABSZ";
			default:
				break;
			}
			break;
		case EM_MIPS:
		case EM_MIPS_RS3_LE:
			switch (dtype) {
			case DT_MIPS_RLD_VERSION:
				return "MIPS_RLD_VERSION";
			case DT_MIPS_TIME_STAMP:
				return "MIPS_TIME_STAMP";
			case DT_MIPS_ICHECKSUM:
				return "MIPS_ICHECKSUM";
			case DT_MIPS_IVERSION:
				return "MIPS_IVERSION";
			case DT_MIPS_FLAGS:
				return "MIPS_FLAGS";
			case DT_MIPS_BASE_ADDRESS:
				return "MIPS_BASE_ADDRESS";
			case DT_MIPS_CONFLICT:
				return "MIPS_CONFLICT";
			case DT_MIPS_LIBLIST:
				return "MIPS_LIBLIST";
			case DT_MIPS_LOCAL_GOTNO:
				return "MIPS_LOCAL_GOTNO";
			case DT_MIPS_CONFLICTNO:
				return "MIPS_CONFLICTNO";
			case DT_MIPS_LIBLISTNO:
				return "MIPS_LIBLISTNO";
			case DT_MIPS_SYMTABNO:
				return "MIPS_SYMTABNO";
			case DT_MIPS_UNREFEXTNO:
				return "MIPS_UNREFEXTNO";
			case DT_MIPS_GOTSYM:
				return "MIPS_GOTSYM";
			case DT_MIPS_HIPAGENO:
				return "MIPS_HIPAGENO";
			case DT_MIPS_RLD_MAP:
				return "MIPS_RLD_MAP";
			case DT_MIPS_DELTA_CLASS:
				return "MIPS_DELTA_CLASS";
			case DT_MIPS_DELTA_CLASS_NO:
				return "MIPS_DELTA_CLASS_NO";
			case DT_MIPS_DELTA_INSTANCE:
				return "MIPS_DELTA_INSTANCE";
			case DT_MIPS_DELTA_INSTANCE_NO:
				return "MIPS_DELTA_INSTANCE_NO";
			case DT_MIPS_DELTA_RELOC:
				return "MIPS_DELTA_RELOC";
			case DT_MIPS_DELTA_RELOC_NO:
				return "MIPS_DELTA_RELOC_NO";
			case DT_MIPS_DELTA_SYM:
				return "MIPS_DELTA_SYM";
			case DT_MIPS_DELTA_SYM_NO:
				return "MIPS_DELTA_SYM_NO";
			case DT_MIPS_DELTA_CLASSSYM:
				return "MIPS_DELTA_CLASSSYM";
			case DT_MIPS_DELTA_CLASSSYM_NO:
				return "MIPS_DELTA_CLASSSYM_NO";
			case DT_MIPS_CXX_FLAGS:
				return "MIPS_CXX_FLAGS";
			case DT_MIPS_PIXIE_INIT:
				return "MIPS_PIXIE_INIT";
			case DT_MIPS_SYMBOL_LIB:
				return "MIPS_SYMBOL_LIB";
			case DT_MIPS_LOCALPAGE_GOTIDX:
				return "MIPS_LOCALPAGE_GOTIDX";
			case DT_MIPS_LOCAL_GOTIDX:
				return "MIPS_LOCAL_GOTIDX";
			case DT_MIPS_HIDDEN_GOTIDX:
				return "MIPS_HIDDEN_GOTIDX";
			case DT_MIPS_PROTECTED_GOTIDX:
				return "MIPS_PROTECTED_GOTIDX";
			case DT_MIPS_OPTIONS:
				return "MIPS_OPTIONS";
			case DT_MIPS_INTERFACE:
				return "MIPS_INTERFACE";
			case DT_MIPS_DYNSTR_ALIGN:
				return "MIPS_DYNSTR_ALIGN";
			case DT_MIPS_INTERFACE_SIZE:
				return "MIPS_INTERFACE_SIZE";
			case DT_MIPS_RLD_TEXT_RESOLVE_ADDR:
				return "MIPS_RLD_TEXT_RESOLVE_ADDR";
			case DT_MIPS_PERF_SUFFIX:
				return "MIPS_PERF_SUFFIX";
			case DT_MIPS_COMPACT_SIZE:
				return "MIPS_COMPACT_SIZE";
			case DT_MIPS_GP_VALUE:
				return "MIPS_GP_VALUE";
			case DT_MIPS_AUX_DYNAMIC:
				return "MIPS_AUX_DYNAMIC";
			case DT_MIPS_PLTGOT:
				return "MIPS_PLTGOT";
			case DT_MIPS_RLD_OBJ_UPDATE:
				return "MIPS_RLD_OBJ_UPDATE";
			case DT_MIPS_RWPLT:
				return "MIPS_RWPLT";
			default:
				break;
			}
			break;
		case EM_SPARC:
		case EM_SPARC32PLUS:
		case EM_SPARCV9:
			switch (dtype) {
			case DT_SPARC_REGISTER:
				return "DT_SPARC_REGISTER";
			default:
				break;
			}
			break;
		default:
			break;
		}
		snprintf(s_dtype, sizeof(s_dtype), "<unknown: %#x>", dtype);
		return (s_dtype);
	}
#endif /* 0 */

	switch (dtype) {
	case DT_NULL: return "NULL";
	case DT_NEEDED: return "NEEDED";
	case DT_PLTRELSZ: return "PLTRELSZ";
	case DT_PLTGOT: return "PLTGOT";
	case DT_HASH: return "HASH";
	case DT_STRTAB: return "STRTAB";
	case DT_SYMTAB: return "SYMTAB";
	case DT_RELA: return "RELA";
	case DT_RELASZ: return "RELASZ";
	case DT_RELAENT: return "RELAENT";
	case DT_STRSZ: return "STRSZ";
	case DT_SYMENT: return "SYMENT";
	case DT_INIT: return "INIT";
	case DT_FINI: return "FINI";
	case DT_SONAME: return "SONAME";
	case DT_RPATH: return "RPATH";
	case DT_SYMBOLIC: return "SYMBOLIC";
	case DT_REL: return "REL";
	case DT_RELSZ: return "RELSZ";
	case DT_RELENT: return "RELENT";
	case DT_PLTREL: return "PLTREL";
	case DT_DEBUG: return "DEBUG";
	case DT_TEXTREL: return "TEXTREL";
	case DT_JMPREL: return "JMPREL";
	case DT_BIND_NOW: return "BIND_NOW";
	case DT_INIT_ARRAY: return "INIT_ARRAY";
	case DT_FINI_ARRAY: return "FINI_ARRAY";
	case DT_INIT_ARRAYSZ: return "INIT_ARRAYSZ";
	case DT_FINI_ARRAYSZ: return "FINI_ARRAYSZ";
	case DT_RUNPATH: return "RUNPATH";
	case DT_FLAGS: return "FLAGS";
	case DT_PREINIT_ARRAY: return "PREINIT_ARRAY";
	case DT_PREINIT_ARRAYSZ: return "PREINIT_ARRAYSZ";
//	case DT_MAXPOSTAGS: return "MAXPOSTAGS";
//	case DT_SUNW_AUXILIARY: return "SUNW_AUXILIARY";
//	case DT_SUNW_RTLDINF: return "SUNW_RTLDINF";
//	case DT_SUNW_FILTER: return "SUNW_FILTER";
//	case DT_SUNW_CAP: return "SUNW_CAP";
	case DT_CHECKSUM: return "CHECKSUM";
	case DT_PLTPADSZ: return "PLTPADSZ";
	case DT_MOVEENT: return "MOVEENT";
	case DT_MOVESZ: return "MOVESZ";
	case DT_FEATURE_1: return "FEATURE_1";
	case DT_POSFLAG_1: return "POSFLAG_1";
	case DT_SYMINSZ: return "SYMINSZ";
	case DT_SYMINENT: return "SYMINENT";
//	case DT_GNU_HASH: return "GNU_HASH";
//	case DT_GNU_CONFLICT: return "GNU_CONFLICT";
//	case DT_GNU_LIBLIST: return "GNU_LIBLIST";
	case DT_CONFIG: return "CONFIG";
	case DT_DEPAUDIT: return "DEPAUDIT";
	case DT_AUDIT: return "AUDIT";
	case DT_PLTPAD: return "PLTPAD";
	case DT_MOVETAB: return "MOVETAB";
	case DT_SYMINFO: return "SYMINFO";
	case DT_VERSYM: return "VERSYM";
	case DT_RELACOUNT: return "RELACOUNT";
	case DT_RELCOUNT: return "RELCOUNT";
	case DT_FLAGS_1: return "FLAGS_1";
	case DT_VERDEF: return "VERDEF";
	case DT_VERDEFNUM: return "VERDEFNUM";
	case DT_VERNEED: return "VERNEED";
	case DT_VERNEEDNUM: return "VERNEEDNUM";
	case DT_AUXILIARY: return "AUXILIARY";
	case DT_USED: return "USED";
	case DT_FILTER: return "FILTER";
//	case DT_GNU_PRELINKED: return "GNU_PRELINKED";
//	case DT_GNU_CONFLICTSZ: return "GNU_CONFLICTSZ";
//	case DT_GNU_LIBLISTSZ: return "GNU_LIBLISTSZ";
	default:
		snprintf(s_dtype, sizeof(s_dtype), "<unknown: %#x>", dtype);
		return (s_dtype);
	}
}

static const char *
st_bind(unsigned int sbind)
{
	static char s_sbind[32];

	switch (sbind) {
	case STB_LOCAL: return "LOCAL";
	case STB_GLOBAL: return "GLOBAL";
	case STB_WEAK: return "WEAK";
	default:
		if (sbind >= STB_LOOS && sbind <= STB_HIOS)
			return "OS";
		else if (sbind >= STB_LOPROC && sbind <= STB_HIPROC)
			return "PROC";
		else
			snprintf(s_sbind, sizeof(s_sbind), "<unknown: %#x>",
			    sbind);
		return (s_sbind);
	}
}

static const char *
st_type(unsigned int stype)
{
	static char s_stype[32];

	switch (stype) {
	case STT_NOTYPE: return "NOTYPE";
	case STT_OBJECT: return "OBJECT";
	case STT_FUNC: return "FUNC";
	case STT_SECTION: return "SECTION";
	case STT_FILE: return "FILE";
	case STT_COMMON: return "COMMON";
	case STT_TLS: return "TLS";
	default:
		if (stype >= STT_LOOS && stype <= STT_HIOS)
			snprintf(s_stype, sizeof(s_stype), "OS+%#x",
			    stype - STT_LOOS);
		else if (stype >= STT_LOPROC && stype <= STT_HIPROC)
			snprintf(s_stype, sizeof(s_stype), "PROC+%#x",
			    stype - STT_LOPROC);
		else
			snprintf(s_stype, sizeof(s_stype), "<unknown: %#x>",
			    stype);
		return (s_stype);
	}
}

static const char *
st_vis(unsigned int svis)
{
	static char s_svis[32];

	switch(svis) {
	case STV_DEFAULT: return "DEFAULT";
	case STV_INTERNAL: return "INTERNAL";
	case STV_HIDDEN: return "HIDDEN";
	case STV_PROTECTED: return "PROTECTED";
	default:
		snprintf(s_svis, sizeof(s_svis), "<unknown: %#x>", svis);
		return (s_svis);
	}
}

static const char *
st_shndx(unsigned int shndx)
{
	static char s_shndx[32];

	switch (shndx) {
	case SHN_UNDEF: return "UND";
	case SHN_ABS: return "ABS";
	case SHN_COMMON: return "COM";
	default:
		if (shndx >= SHN_LOPROC && shndx <= SHN_HIPROC)
			return "PRC";
		else if (shndx >= SHN_LOOS && shndx <= SHN_HIOS)
			return "OS";
		else
			snprintf(s_shndx, sizeof(s_shndx), "%u", shndx);
		return (s_shndx);
	}
}

static struct {
	const char *ln;
	char sn;
	int value;
} section_flag[] = {
	{"WRITE", 'W', SHF_WRITE},
	{"ALLOC", 'A', SHF_ALLOC},
	{"EXEC", 'X', SHF_EXECINSTR},
	{"MERGE", 'M', SHF_MERGE},
	{"STRINGS", 'S', SHF_STRINGS},
	{"INFO LINK", 'I', SHF_INFO_LINK},
	{"OS NONCONF", 'O', SHF_OS_NONCONFORMING},
	{"GROUP", 'G', SHF_GROUP},
	{"TLS", 'T', SHF_TLS},
	{NULL, 0, 0}
};

static const char *
r_type(unsigned int mach, unsigned int type)
{
	switch(mach) {
	case EM_NONE: return "";
	case EM_386:
		switch(type) {
		case 0: return "R_386_NONE";
		case 1: return "R_386_32";
		case 2: return "R_386_PC32";
		case 3: return "R_386_GOT32";
		case 4: return "R_386_PLT32";
		case 5: return "R_386_COPY";
		case 6: return "R_386_GLOB_DAT";
		case 7: return "R_386_JMP_SLOT";
		case 8: return "R_386_RELATIVE";
		case 9: return "R_386_GOTOFF";
		case 10: return "R_386_GOTPC";
		case 14: return "R_386_TLS_TPOFF";
		case 15: return "R_386_TLS_IE";
		case 16: return "R_386_TLS_GOTIE";
		case 17: return "R_386_TLS_LE";
		case 18: return "R_386_TLS_GD";
		case 19: return "R_386_TLS_LDM";
		case 24: return "R_386_TLS_GD_32";
		case 25: return "R_386_TLS_GD_PUSH";
		case 26: return "R_386_TLS_GD_CALL";
		case 27: return "R_386_TLS_GD_POP";
		case 28: return "R_386_TLS_LDM_32";
		case 29: return "R_386_TLS_LDM_PUSH";
		case 30: return "R_386_TLS_LDM_CALL";
		case 31: return "R_386_TLS_LDM_POP";
		case 32: return "R_386_TLS_LDO_32";
		case 33: return "R_386_TLS_IE_32";
		case 34: return "R_386_TLS_LE_32";
		case 35: return "R_386_TLS_DTPMOD32";
		case 36: return "R_386_TLS_DTPOFF32";
		case 37: return "R_386_TLS_TPOFF32";
		default: return "";
		}
#if 0
	case EM_ARM:
		switch(type) {
		case 0: return "R_ARM_NONE";
		case 1: return "R_ARM_PC24";
		case 2: return "R_ARM_ABS32";
		case 3: return "R_ARM_REL32";
		case 4: return "R_ARM_PC13";
		case 5: return "R_ARM_ABS16";
		case 6: return "R_ARM_ABS12";
		case 7: return "R_ARM_THM_ABS5";
		case 8: return "R_ARM_ABS8";
		case 9: return "R_ARM_SBREL32";
		case 10: return "R_ARM_THM_PC22";
		case 11: return "R_ARM_THM_PC8";
		case 12: return "R_ARM_AMP_VCALL9";
		case 13: return "R_ARM_SWI24";
		case 14: return "R_ARM_THM_SWI8";
		case 15: return "R_ARM_XPC25";
		case 16: return "R_ARM_THM_XPC22";
		case 20: return "R_ARM_COPY";
		case 21: return "R_ARM_GLOB_DAT";
		case 22: return "R_ARM_JUMP_SLOT";
		case 23: return "R_ARM_RELATIVE";
		case 24: return "R_ARM_GOTOFF";
		case 25: return "R_ARM_GOTPC";
		case 26: return "R_ARM_GOT32";
		case 27: return "R_ARM_PLT32";
		case 100: return "R_ARM_GNU_VTENTRY";
		case 101: return "R_ARM_GNU_VTINHERIT";
		case 250: return "R_ARM_RSBREL32";
		case 251: return "R_ARM_THM_RPC22";
		case 252: return "R_ARM_RREL32";
		case 253: return "R_ARM_RABS32";
		case 254: return "R_ARM_RPC24";
		case 255: return "R_ARM_RBASE";
		default: return "";
		}
	case EM_IA_64:
		switch(type) {
		case 0: return "R_IA_64_NONE";
		case 33: return "R_IA_64_IMM14";
		case 34: return "R_IA_64_IMM22";
		case 35: return "R_IA_64_IMM64";
		case 36: return "R_IA_64_DIR32MSB";
		case 37: return "R_IA_64_DIR32LSB";
		case 38: return "R_IA_64_DIR64MSB";
		case 39: return "R_IA_64_DIR64LSB";
		case 42: return "R_IA_64_GPREL22";
		case 43: return "R_IA_64_GPREL64I";
		case 44: return "R_IA_64_GPREL32MSB";
		case 45: return "R_IA_64_GPREL32LSB";
		case 46: return "R_IA_64_GPREL64MSB";
		case 47: return "R_IA_64_GPREL64LSB";
		case 50: return "R_IA_64_LTOFF22";
		case 51: return "R_IA_64_LTOFF64I";
		case 58: return "R_IA_64_PLTOFF22";
		case 59: return "R_IA_64_PLTOFF64I";
		case 62: return "R_IA_64_PLTOFF64MSB";
		case 63: return "R_IA_64_PLTOFF64LSB";
		case 67: return "R_IA_64_FPTR64I";
		case 68: return "R_IA_64_FPTR32MSB";
		case 69: return "R_IA_64_FPTR32LSB";
		case 70: return "R_IA_64_FPTR64MSB";
		case 71: return "R_IA_64_FPTR64LSB";
		case 72: return "R_IA_64_PCREL60B";
		case 73: return "R_IA_64_PCREL21B";
		case 74: return "R_IA_64_PCREL21M";
		case 75: return "R_IA_64_PCREL21F";
		case 76: return "R_IA_64_PCREL32MSB";
		case 77: return "R_IA_64_PCREL32LSB";
		case 78: return "R_IA_64_PCREL64MSB";
		case 79: return "R_IA_64_PCREL64LSB";
		case 82: return "R_IA_64_LTOFF_FPTR22";
		case 83: return "R_IA_64_LTOFF_FPTR64I";
		case 84: return "R_IA_64_LTOFF_FPTR32MSB";
		case 85: return "R_IA_64_LTOFF_FPTR32LSB";
		case 86: return "R_IA_64_LTOFF_FPTR64MSB";
		case 87: return "R_IA_64_LTOFF_FPTR64LSB";
		case 92: return "R_IA_64_SEGREL32MSB";
		case 93: return "R_IA_64_SEGREL32LSB";
		case 94: return "R_IA_64_SEGREL64MSB";
		case 95: return "R_IA_64_SEGREL64LSB";
		case 100: return "R_IA_64_SECREL32MSB";
		case 101: return "R_IA_64_SECREL32LSB";
		case 102: return "R_IA_64_SECREL64MSB";
		case 103: return "R_IA_64_SECREL64LSB";
		case 108: return "R_IA_64_REL32MSB";
		case 109: return "R_IA_64_REL32LSB";
		case 110: return "R_IA_64_REL64MSB";
		case 111: return "R_IA_64_REL64LSB";
		case 116: return "R_IA_64_LTV32MSB";
		case 117: return "R_IA_64_LTV32LSB";
		case 118: return "R_IA_64_LTV64MSB";
		case 119: return "R_IA_64_LTV64LSB";
		case 121: return "R_IA_64_PCREL21BI";
		case 122: return "R_IA_64_PCREL22";
		case 123: return "R_IA_64_PCREL64I";
		case 128: return "R_IA_64_IPLTMSB";
		case 129: return "R_IA_64_IPLTLSB";
		case 133: return "R_IA_64_SUB";
		case 134: return "R_IA_64_LTOFF22X";
		case 135: return "R_IA_64_LDXMOV";
		case 145: return "R_IA_64_TPREL14";
		case 146: return "R_IA_64_TPREL22";
		case 147: return "R_IA_64_TPREL64I";
		case 150: return "R_IA_64_TPREL64MSB";
		case 151: return "R_IA_64_TPREL64LSB";
		case 154: return "R_IA_64_LTOFF_TPREL22";
		case 166: return "R_IA_64_DTPMOD64MSB";
		case 167: return "R_IA_64_DTPMOD64LSB";
		case 170: return "R_IA_64_LTOFF_DTPMOD22";
		case 177: return "R_IA_64_DTPREL14";
		case 178: return "R_IA_64_DTPREL22";
		case 179: return "R_IA_64_DTPREL64I";
		case 180: return "R_IA_64_DTPREL32MSB";
		case 181: return "R_IA_64_DTPREL32LSB";
		case 182: return "R_IA_64_DTPREL64MSB";
		case 183: return "R_IA_64_DTPREL64LSB";
		case 186: return "R_IA_64_LTOFF_DTPREL22";
		default: return "";
		}
	case EM_MIPS:
		switch(type) {
		case 0: return "R_MIPS_NONE";
		case 1: return "R_MIPS_16";
		case 2: return "R_MIPS_32";
		case 3: return "R_MIPS_REL32";
		case 4: return "R_MIPS_26";
		case 5: return "R_MIPS_HI16";
		case 6: return "R_MIPS_LO16";
		case 7: return "R_MIPS_GPREL16";
		case 8: return "R_MIPS_LITERAL";
		case 9: return "R_MIPS_GOT16";
		case 10: return "R_MIPS_PC16";
		case 11: return "R_MIPS_CALL16";
		case 12: return "R_MIPS_GPREL32";
		case 21: return "R_MIPS_GOTHI16";
		case 22: return "R_MIPS_GOTLO16";
		case 30: return "R_MIPS_CALLHI16";
		case 31: return "R_MIPS_CALLLO16";
		default: return "";
		}
	case EM_PPC:
		switch(type) {
		case 0: return "R_PPC_NONE";
		case 1: return "R_PPC_ADDR32";
		case 2: return "R_PPC_ADDR24";
		case 3: return "R_PPC_ADDR16";
		case 4: return "R_PPC_ADDR16_LO";
		case 5: return "R_PPC_ADDR16_HI";
		case 6: return "R_PPC_ADDR16_HA";
		case 7: return "R_PPC_ADDR14";
		case 8: return "R_PPC_ADDR14_BRTAKEN";
		case 9: return "R_PPC_ADDR14_BRNTAKEN";
		case 10: return "R_PPC_REL24";
		case 11: return "R_PPC_REL14";
		case 12: return "R_PPC_REL14_BRTAKEN";
		case 13: return "R_PPC_REL14_BRNTAKEN";
		case 14: return "R_PPC_GOT16";
		case 15: return "R_PPC_GOT16_LO";
		case 16: return "R_PPC_GOT16_HI";
		case 17: return "R_PPC_GOT16_HA";
		case 18: return "R_PPC_PLTREL24";
		case 19: return "R_PPC_COPY";
		case 20: return "R_PPC_GLOB_DAT";
		case 21: return "R_PPC_JMP_SLOT";
		case 22: return "R_PPC_RELATIVE";
		case 23: return "R_PPC_LOCAL24PC";
		case 24: return "R_PPC_UADDR32";
		case 25: return "R_PPC_UADDR16";
		case 26: return "R_PPC_REL32";
		case 27: return "R_PPC_PLT32";
		case 28: return "R_PPC_PLTREL32";
		case 29: return "R_PPC_PLT16_LO";
		case 30: return "R_PPC_PLT16_HI";
		case 31: return "R_PPC_PLT16_HA";
		case 32: return "R_PPC_SDAREL16";
		case 33: return "R_PPC_SECTOFF";
		case 34: return "R_PPC_SECTOFF_LO";
		case 35: return "R_PPC_SECTOFF_HI";
		case 36: return "R_PPC_SECTOFF_HA";
		case 67: return "R_PPC_TLS";
		case 68: return "R_PPC_DTPMOD32";
		case 69: return "R_PPC_TPREL16";
		case 70: return "R_PPC_TPREL16_LO";
		case 71: return "R_PPC_TPREL16_HI";
		case 72: return "R_PPC_TPREL16_HA";
		case 73: return "R_PPC_TPREL32";
		case 74: return "R_PPC_DTPREL16";
		case 75: return "R_PPC_DTPREL16_LO";
		case 76: return "R_PPC_DTPREL16_HI";
		case 77: return "R_PPC_DTPREL16_HA";
		case 78: return "R_PPC_DTPREL32";
		case 79: return "R_PPC_GOT_TLSGD16";
		case 80: return "R_PPC_GOT_TLSGD16_LO";
		case 81: return "R_PPC_GOT_TLSGD16_HI";
		case 82: return "R_PPC_GOT_TLSGD16_HA";
		case 83: return "R_PPC_GOT_TLSLD16";
		case 84: return "R_PPC_GOT_TLSLD16_LO";
		case 85: return "R_PPC_GOT_TLSLD16_HI";
		case 86: return "R_PPC_GOT_TLSLD16_HA";
		case 87: return "R_PPC_GOT_TPREL16";
		case 88: return "R_PPC_GOT_TPREL16_LO";
		case 89: return "R_PPC_GOT_TPREL16_HI";
		case 90: return "R_PPC_GOT_TPREL16_HA";
		case 101: return "R_PPC_EMB_NADDR32";
		case 102: return "R_PPC_EMB_NADDR16";
		case 103: return "R_PPC_EMB_NADDR16_LO";
		case 104: return "R_PPC_EMB_NADDR16_HI";
		case 105: return "R_PPC_EMB_NADDR16_HA";
		case 106: return "R_PPC_EMB_SDAI16";
		case 107: return "R_PPC_EMB_SDA2I16";
		case 108: return "R_PPC_EMB_SDA2REL";
		case 109: return "R_PPC_EMB_SDA21";
		case 110: return "R_PPC_EMB_MRKREF";
		case 111: return "R_PPC_EMB_RELSEC16";
		case 112: return "R_PPC_EMB_RELST_LO";
		case 113: return "R_PPC_EMB_RELST_HI";
		case 114: return "R_PPC_EMB_RELST_HA";
		case 115: return "R_PPC_EMB_BIT_FLD";
		case 116: return "R_PPC_EMB_RELSDA";
		default: return "";
		}
	case EM_SPARC:
	case EM_SPARCV9:
		switch(type) {
		case 0: return "R_SPARC_NONE";
		case 1: return "R_SPARC_8";
		case 2: return "R_SPARC_16";
		case 3: return "R_SPARC_32";
		case 4: return "R_SPARC_DISP8";
		case 5: return "R_SPARC_DISP16";
		case 6: return "R_SPARC_DISP32";
		case 7: return "R_SPARC_WDISP30";
		case 8: return "R_SPARC_WDISP22";
		case 9: return "R_SPARC_HI22";
		case 10: return "R_SPARC_22";
		case 11: return "R_SPARC_13";
		case 12: return "R_SPARC_LO10";
		case 13: return "R_SPARC_GOT10";
		case 14: return "R_SPARC_GOT13";
		case 15: return "R_SPARC_GOT22";
		case 16: return "R_SPARC_PC10";
		case 17: return "R_SPARC_PC22";
		case 18: return "R_SPARC_WPLT30";
		case 19: return "R_SPARC_COPY";
		case 20: return "R_SPARC_GLOB_DAT";
		case 21: return "R_SPARC_JMP_SLOT";
		case 22: return "R_SPARC_RELATIVE";
		case 23: return "R_SPARC_UA32";
		case 24: return "R_SPARC_PLT32";
		case 25: return "R_SPARC_HIPLT22";
		case 26: return "R_SPARC_LOPLT10";
		case 27: return "R_SPARC_PCPLT32";
		case 28: return "R_SPARC_PCPLT22";
		case 29: return "R_SPARC_PCPLT10";
		case 30: return "R_SPARC_10";
		case 31: return "R_SPARC_11";
		case 32: return "R_SPARC_64";
		case 33: return "R_SPARC_OLO10";
		case 34: return "R_SPARC_HH22";
		case 35: return "R_SPARC_HM10";
		case 36: return "R_SPARC_LM22";
		case 37: return "R_SPARC_PC_HH22";
		case 38: return "R_SPARC_PC_HM10";
		case 39: return "R_SPARC_PC_LM22";
		case 40: return "R_SPARC_WDISP16";
		case 41: return "R_SPARC_WDISP19";
		case 42: return "R_SPARC_GLOB_JMP";
		case 43: return "R_SPARC_7";
		case 44: return "R_SPARC_5";
		case 45: return "R_SPARC_6";
		case 46: return "R_SPARC_DISP64";
		case 47: return "R_SPARC_PLT64";
		case 48: return "R_SPARC_HIX22";
		case 49: return "R_SPARC_LOX10";
		case 50: return "R_SPARC_H44";
		case 51: return "R_SPARC_M44";
		case 52: return "R_SPARC_L44";
		case 53: return "R_SPARC_REGISTER";
		case 54: return "R_SPARC_UA64";
		case 55: return "R_SPARC_UA16";
		case 56: return "R_SPARC_TLS_GD_HI22";
		case 57: return "R_SPARC_TLS_GD_LO10";
		case 58: return "R_SPARC_TLS_GD_ADD";
		case 59: return "R_SPARC_TLS_GD_CALL";
		case 60: return "R_SPARC_TLS_LDM_HI22";
		case 61: return "R_SPARC_TLS_LDM_LO10";
		case 62: return "R_SPARC_TLS_LDM_ADD";
		case 63: return "R_SPARC_TLS_LDM_CALL";
		case 64: return "R_SPARC_TLS_LDO_HIX22";
		case 65: return "R_SPARC_TLS_LDO_LOX10";
		case 66: return "R_SPARC_TLS_LDO_ADD";
		case 67: return "R_SPARC_TLS_IE_HI22";
		case 68: return "R_SPARC_TLS_IE_LO10";
		case 69: return "R_SPARC_TLS_IE_LD";
		case 70: return "R_SPARC_TLS_IE_LDX";
		case 71: return "R_SPARC_TLS_IE_ADD";
		case 72: return "R_SPARC_TLS_LE_HIX22";
		case 73: return "R_SPARC_TLS_LE_LOX10";
		case 74: return "R_SPARC_TLS_DTPMOD32";
		case 75: return "R_SPARC_TLS_DTPMOD64";
		case 76: return "R_SPARC_TLS_DTPOFF32";
		case 77: return "R_SPARC_TLS_DTPOFF64";
		case 78: return "R_SPARC_TLS_TPOFF32";
		case 79: return "R_SPARC_TLS_TPOFF64";
		default: return "";
		}
#endif /* 0 */
	case EM_X86_64:
		switch(type) {
		case 0: return "R_X86_64_NONE";
		case 1: return "R_X86_64_64";
		case 2: return "R_X86_64_PC32";
		case 3: return "R_X86_64_GOT32";
		case 4: return "R_X86_64_PLT32";
		case 5: return "R_X86_64_COPY";
		case 6: return "R_X86_64_GLOB_DAT";
		case 7: return "R_X86_64_JMP_SLOT";
		case 8: return "R_X86_64_RELATIVE";
		case 9: return "R_X86_64_GOTPCREL";
		case 10: return "R_X86_64_32";
		case 11: return "R_X86_64_32S";
		case 12: return "R_X86_64_16";
		case 13: return "R_X86_64_PC16";
		case 14: return "R_X86_64_8";
		case 15: return "R_X86_64_PC8";
		case 16: return "R_X86_64_DTPMOD64";
		case 17: return "R_X86_64_DTPOFF64";
		case 18: return "R_X86_64_TPOFF64";
		case 19: return "R_X86_64_TLSGD";
		case 20: return "R_X86_64_TLSLD";
		case 21: return "R_X86_64_DTPOFF32";
		case 22: return "R_X86_64_GOTTPOFF";
		case 23: return "R_X86_64_TPOFF32";
		default: return "";
		}
	default: return "";
	}
}

static const char *
note_type(unsigned int osabi, unsigned int et, unsigned int nt)
{
	static char s_nt[32];

	if (et == ET_CORE) {
		switch (nt) {
		case NT_PRSTATUS:
			return "NT_PRSTATUS (Process status)";
//		case NT_FPREGSET:
//			return "NT_FPREGSET (Floating point information)";
		case NT_PRPSINFO:
			return "NT_PRPSINFO (Process information)";
//		case NT_AUXV:
//			return "NT_AUXV (Auxiliary vector)";
//		case NT_PRXFPREG:
//			return "NT_PRXFPREG (Linux user_xfpregs structure)";
//		case NT_PSTATUS:
//			return "NT_PSTATUS (Linux process status)";
//		case NT_FPREGS:
//			return "NT_FPREGS (Linux floating point regset)";
//		case NT_PSINFO:
//			return "NT_PSINFO (Linux process information)";
//		case NT_LWPSTATUS:
//			return "NT_LWPSTATUS (Linux lwpstatus_t type)";
//		case NT_LWPSINFO:
//			return "NT_LWPSINFO (Linux lwpinfo_t type)";
		default:
			snprintf(s_nt, sizeof(s_nt), "<unknown: %u>", nt);
			return (s_nt);
		}
	} else {
		switch (nt) {
#if 0
		case NT_ABI_TAG:
			switch (osabi) {
			case CGCEFOSABI_FREEBSD:
				return "NT_FREEBSD_ABI_TAG";
			case CGCEFOSABI_NETBSD:
				return "NT_NETBSD_IDENT";
			case CGCEFOSABI_OPENBSD:
				return "NT_OPENBSD_IDENT";
			default:
				return "NT_GNU_ABI_TAG";
			}
		case NT_GNU_HWCAP:
			return "NT_GNU_HWCAP (Hardware capabilities)";
		case NT_GNU_BUILD_ID:
			return "NT_GNU_BUILD_ID (Build id set by ld(1))";
		case NT_GNU_GOLD_VERSION:
			return "NT_GNU_GOLD_VERSION (GNU gold version)";
#endif /* 0 */
		default:
			snprintf(s_nt, sizeof(s_nt), "<unknown: %u>", nt);
			return (s_nt);
		}
	}
}

static struct {
	const char *name;
	int value;
} l_flag[] = {
//	{"EXACT_MATCH", LL_EXACT_MATCH},
//	{"IGNORE_INT_VER", LL_IGNORE_INT_VER},
//	{"REQUIRE_MINOR", LL_REQUIRE_MINOR},
//	{"EXPORTS", LL_EXPORTS},
//	{"DELAY_LOAD", LL_DELAY_LOAD},
//	{"DELTA", LL_DELTA},
	{NULL, 0}
};

static struct mips_option mips_exceptions_option[] = {
//	{OEX_PAGE0, "PAGE0"},
//	{OEX_SMM, "SMM"},
//	{OEX_PRECISEFP, "PRECISEFP"},
//	{OEX_DISMISS, "DISMISS"},
	{0, NULL}
};

static struct mips_option mips_pad_option[] = {
//	{OPAD_PREFIX, "PREFIX"},
//	{OPAD_POSTFIX, "POSTFIX"},
//	{OPAD_SYMBOL, "SYMBOL"},
	{0, NULL}
};

static struct mips_option mips_hwpatch_option[] = {
//	{OHW_R4KEOP, "R4KEOP"},
//	{OHW_R8KPFETCH, "R8KPFETCH"},
//	{OHW_R5KEOP, "R5KEOP"},
//	{OHW_R5KCVTL, "R5KCVTL"},
	{0, NULL}
};

static struct mips_option mips_hwa_option[] = {
//	{OHWA0_R4KEOP_CHECKED, "R4KEOP_CHECKED"},
//	{OHWA0_R4KEOP_CLEAN, "R4KEOP_CLEAN"},
	{0, NULL}
};

static struct mips_option mips_hwo_option[] = {
//	{OHWO0_FIXADE, "FIXADE"},
	{0, NULL}
};

static const char *
option_kind(uint8_t kind)
{
	static char s_kind[32];

	switch (kind) {
#if 0
	case ODK_NULL: return "NULL";
	case ODK_REGINFO: return "REGINFO";
	case ODK_EXCEPTIONS: return "EXCEPTIONS";
	case ODK_PAD: return "PAD";
	case ODK_HWPATCH: return "HWPATCH";
	case ODK_FILL: return "FILL";
	case ODK_TAGS: return "TAGS";
	case ODK_HWAND: return "HWAND";
	case ODK_HWOR: return "HWOR";
	case ODK_GP_GROUP: return "GP_GROUP";
	case ODK_IDENT: return "IDENT";
#endif /* 0 */
	default:
		snprintf(s_kind, sizeof(s_kind), "<unknown: %u>", kind);
		return (s_kind);
	}
}

static const char *
top_tag(unsigned int tag)
{
	static char s_top_tag[32];

	switch (tag) {
	case 1: return "File Attributes";
	case 2: return "Section Attributes";
	case 3: return "Symbol Attributes";
	default:
		snprintf(s_top_tag, sizeof(s_top_tag), "Unknown tag: %u", tag);
		return (s_top_tag);
	}
}

static const char *
aeabi_cpu_arch(uint64_t arch)
{
	static char s_cpu_arch[32];

	switch (arch) {
	case 0: return "Pre-V4";
	case 1: return "ARM v4";
	case 2: return "ARM v4T";
	case 3: return "ARM v5T";
	case 4: return "ARM v5TE";
	case 5: return "ARM v5TEJ";
	case 6: return "ARM v6";
	case 7: return "ARM v6KZ";
	case 8: return "ARM v6T2";
	case 9: return "ARM v6K";
	case 10: return "ARM v7";
	case 11: return "ARM v6-M";
	case 12: return "ARM v6S-M";
	case 13: return "ARM v7E-M";
	default:
		snprintf(s_cpu_arch, sizeof(s_cpu_arch),
		    "Unknown (%ju)", (uintmax_t) arch);
		return (s_cpu_arch);
	}
}

static const char *
aeabi_cpu_arch_profile(uint64_t pf)
{
	static char s_arch_profile[32];

	switch (pf) {
	case 0:
		return "Not applicable";
	case 0x41:		/* 'A' */
		return "Application Profile";
	case 0x52:		/* 'R' */
		return "Real-Time Profile";
	case 0x4D:		/* 'M' */
		return "Microcontroller Profile";
	case 0x53:		/* 'S' */
		return "Application or Real-Time Profile";
	default:
		snprintf(s_arch_profile, sizeof(s_arch_profile),
		    "Unknown (%ju)\n", (uintmax_t) pf);
		return (s_arch_profile);
	}
}

static const char *
aeabi_arm_isa(uint64_t ai)
{
	static char s_ai[32];

	switch (ai) {
	case 0: return "No";
	case 1: return "Yes";
	default:
		snprintf(s_ai, sizeof(s_ai), "Unknown (%ju)\n",
		    (uintmax_t) ai);
		return (s_ai);
	}
}

static const char *
aeabi_thumb_isa(uint64_t ti)
{
	static char s_ti[32];

	switch (ti) {
	case 0: return "No";
	case 1: return "16-bit Thumb";
	case 2: return "32-bit Thumb";
	default:
		snprintf(s_ti, sizeof(s_ti), "Unknown (%ju)\n",
		    (uintmax_t) ti);
		return (s_ti);
	}
}

static const char *
aeabi_fp_arch(uint64_t fp)
{
	static char s_fp_arch[32];

	switch (fp) {
	case 0: return "No";
	case 1: return "VFPv1";
	case 2: return "VFPv2";
	case 3: return "VFPv3";
	case 4: return "VFPv3-D16";
	case 5: return "VFPv4";
	case 6: return "VFPv4-D16";
	default:
		snprintf(s_fp_arch, sizeof(s_fp_arch), "Unknown (%ju)",
		    (uintmax_t) fp);
		return (s_fp_arch);
	}
}

static const char *
aeabi_wmmx_arch(uint64_t wmmx)
{
	static char s_wmmx[32];

	switch (wmmx) {
	case 0: return "No";
	case 1: return "WMMXv1";
	case 2: return "WMMXv2";
	default:
		snprintf(s_wmmx, sizeof(s_wmmx), "Unknown (%ju)",
		    (uintmax_t) wmmx);
		return (s_wmmx);
	}
}

static const char *
aeabi_adv_simd_arch(uint64_t simd)
{
	static char s_simd[32];

	switch (simd) {
	case 0: return "No";
	case 1: return "NEONv1";
	case 2: return "NEONv2";
	default:
		snprintf(s_simd, sizeof(s_simd), "Unknown (%ju)",
		    (uintmax_t) simd);
		return (s_simd);
	}
}

static const char *
aeabi_pcs_config(uint64_t pcs)
{
	static char s_pcs[32];

	switch (pcs) {
	case 0: return "None";
	case 1: return "Bare platform";
	case 2: return "Linux";
	case 3: return "Linux DSO";
	case 4: return "Palm OS 2004";
	case 5: return "Palm OS (future)";
	case 6: return "Symbian OS 2004";
	case 7: return "Symbian OS (future)";
	default:
		snprintf(s_pcs, sizeof(s_pcs), "Unknown (%ju)",
		    (uintmax_t) pcs);
		return (s_pcs);
	}
}

static const char *
aeabi_pcs_r9(uint64_t r9)
{
	static char s_r9[32];

	switch (r9) {
	case 0: return "V6";
	case 1: return "SB";
	case 2: return "TLS pointer";
	case 3: return "Unused";
	default:
		snprintf(s_r9, sizeof(s_r9), "Unknown (%ju)", (uintmax_t) r9);
		return (s_r9);
	}
}

static const char *
aeabi_pcs_rw(uint64_t rw)
{
	static char s_rw[32];

	switch (rw) {
	case 0: return "Absolute";
	case 1: return "PC-relative";
	case 2: return "SB-relative";
	case 3: return "None";
	default:
		snprintf(s_rw, sizeof(s_rw), "Unknown (%ju)", (uintmax_t) rw);
		return (s_rw);
	}
}

static const char *
aeabi_pcs_ro(uint64_t ro)
{
	static char s_ro[32];

	switch (ro) {
	case 0: return "Absolute";
	case 1: return "PC-relative";
	case 2: return "None";
	default:
		snprintf(s_ro, sizeof(s_ro), "Unknown (%ju)", (uintmax_t) ro);
		return (s_ro);
	}
}

static const char *
aeabi_pcs_got(uint64_t got)
{
	static char s_got[32];

	switch (got) {
	case 0: return "None";
	case 1: return "direct";
	case 2: return "indirect via GOT";
	default:
		snprintf(s_got, sizeof(s_got), "Unknown (%ju)",
		    (uintmax_t) got);
		return (s_got);
	}
}

static const char *
aeabi_pcs_wchar_t(uint64_t wt)
{
	static char s_wt[32];

	switch (wt) {
	case 0: return "None";
	case 2: return "wchar_t size 2";
	case 4: return "wchar_t size 4";
	default:
		snprintf(s_wt, sizeof(s_wt), "Unknown (%ju)", (uintmax_t) wt);
		return (s_wt);
	}
}

static const char *
aeabi_enum_size(uint64_t es)
{
	static char s_es[32];

	switch (es) {
	case 0: return "None";
	case 1: return "smallest";
	case 2: return "32-bit";
	case 3: return "visible 32-bit";
	default:
		snprintf(s_es, sizeof(s_es), "Unknown (%ju)", (uintmax_t) es);
		return (s_es);
	}
}

static const char *
aeabi_align_needed(uint64_t an)
{
	static char s_align_n[64];

	switch (an) {
	case 0: return "No";
	case 1: return "8-byte align";
	case 2: return "4-byte align";
	case 3: return "Reserved";
	default:
		if (an >= 4 && an <= 12)
			snprintf(s_align_n, sizeof(s_align_n), "8-byte align"
			    " and up to 2^%ju-byte extended align",
			    (uintmax_t) an);
		else
			snprintf(s_align_n, sizeof(s_align_n), "Unknown (%ju)",
			    (uintmax_t) an);
		return (s_align_n);
	}
}

static const char *
aeabi_align_preserved(uint64_t ap)
{
	static char s_align_p[128];

	switch (ap) {
	case 0: return "No";
	case 1: return "8-byte align";
	case 2: return "8-byte align and SP % 8 == 0";
	case 3: return "Reserved";
	default:
		if (ap >= 4 && ap <= 12)
			snprintf(s_align_p, sizeof(s_align_p), "8-byte align"
			    " and SP %% 8 == 0 and up to 2^%ju-byte extended"
			    " align", (uintmax_t) ap);
		else
			snprintf(s_align_p, sizeof(s_align_p), "Unknown (%ju)",
			    (uintmax_t) ap);
		return (s_align_p);
	}
}

static const char *
aeabi_fp_rounding(uint64_t fr)
{
	static char s_fp_r[32];

	switch (fr) {
	case 0: return "Unused";
	case 1: return "Needed";
	default:
		snprintf(s_fp_r, sizeof(s_fp_r), "Unknown (%ju)",
		    (uintmax_t) fr);
		return (s_fp_r);
	}
}

static const char *
aeabi_fp_denormal(uint64_t fd)
{
	static char s_fp_d[32];

	switch (fd) {
	case 0: return "Unused";
	case 1: return "Needed";
	case 2: return "Sign Only";
	default:
		snprintf(s_fp_d, sizeof(s_fp_d), "Unknown (%ju)",
		    (uintmax_t) fd);
		return (s_fp_d);
	}
}

static const char *
aeabi_fp_exceptions(uint64_t fe)
{
	static char s_fp_e[32];

	switch (fe) {
	case 0: return "Unused";
	case 1: return "Needed";
	default:
		snprintf(s_fp_e, sizeof(s_fp_e), "Unknown (%ju)",
		    (uintmax_t) fe);
		return (s_fp_e);
	}
}

static const char *
aeabi_fp_user_exceptions(uint64_t fu)
{
	static char s_fp_u[32];

	switch (fu) {
	case 0: return "Unused";
	case 1: return "Needed";
	default:
		snprintf(s_fp_u, sizeof(s_fp_u), "Unknown (%ju)",
		    (uintmax_t) fu);
		return (s_fp_u);
	}
}

static const char *
aeabi_fp_number_model(uint64_t fn)
{
	static char s_fp_n[32];

	switch (fn) {
	case 0: return "Unused";
	case 1: return "IEEE 754 normal";
	case 2: return "RTABI";
	case 3: return "IEEE 754";
	default:
		snprintf(s_fp_n, sizeof(s_fp_n), "Unknown (%ju)",
		    (uintmax_t) fn);
		return (s_fp_n);
	}
}

static const char *
aeabi_fp_16bit_format(uint64_t fp16)
{
	static char s_fp_16[64];

	switch (fp16) {
	case 0: return "None";
	case 1: return "IEEE 754";
	case 2: return "VFPv3/Advanced SIMD (alternative format)";
	default:
		snprintf(s_fp_16, sizeof(s_fp_16), "Unknown (%ju)",
		    (uintmax_t) fp16);
		return (s_fp_16);
	}
}

static const char *
aeabi_mpext(uint64_t mp)
{
	static char s_mp[32];

	switch (mp) {
	case 0: return "Not allowed";
	case 1: return "Allowed";
	default:
		snprintf(s_mp, sizeof(s_mp), "Unknown (%ju)",
		    (uintmax_t) mp);
		return (s_mp);
	}
}

static const char *
aeabi_div(uint64_t du)
{
	static char s_du[32];

	switch (du) {
	case 0: return "Yes (V7-R/V7-M)";
	case 1: return "No";
	case 2: return "Yes (V7-A)";
	default:
		snprintf(s_du, sizeof(s_du), "Unknown (%ju)",
		    (uintmax_t) du);
		return (s_du);
	}
}

static const char *
aeabi_t2ee(uint64_t t2ee)
{
	static char s_t2ee[32];

	switch (t2ee) {
	case 0: return "Not allowed";
	case 1: return "Allowed";
	default:
		snprintf(s_t2ee, sizeof(s_t2ee), "Unknown(%ju)",
		    (uintmax_t) t2ee);
		return (s_t2ee);
	}

}

static const char *
aeabi_hardfp(uint64_t hfp)
{
	static char s_hfp[32];

	switch (hfp) {
	case 0: return "Tag_FP_arch";
	case 1: return "only SP";
	case 2: return "only DP";
	case 3: return "both SP and DP";
	default:
		snprintf(s_hfp, sizeof(s_hfp), "Unknown (%ju)",
		    (uintmax_t) hfp);
		return (s_hfp);
	}
}

static const char *
aeabi_vfp_args(uint64_t va)
{
	static char s_va[32];

	switch (va) {
	case 0: return "AAPCS (base variant)";
	case 1: return "AAPCS (VFP variant)";
	case 2: return "toolchain-specific";
	default:
		snprintf(s_va, sizeof(s_va), "Unknown (%ju)", (uintmax_t) va);
		return (s_va);
	}
}

static const char *
aeabi_wmmx_args(uint64_t wa)
{
	static char s_wa[32];

	switch (wa) {
	case 0: return "AAPCS (base variant)";
	case 1: return "Intel WMMX";
	case 2: return "toolchain-specific";
	default:
		snprintf(s_wa, sizeof(s_wa), "Unknown(%ju)", (uintmax_t) wa);
		return (s_wa);
	}
}

static const char *
aeabi_unaligned_access(uint64_t ua)
{
	static char s_ua[32];

	switch (ua) {
	case 0: return "Not allowed";
	case 1: return "Allowed";
	default:
		snprintf(s_ua, sizeof(s_ua), "Unknown(%ju)", (uintmax_t) ua);
		return (s_ua);
	}
}

static const char *
aeabi_fp_hpext(uint64_t fh)
{
	static char s_fh[32];

	switch (fh) {
	case 0: return "Not allowed";
	case 1: return "Allowed";
	default:
		snprintf(s_fh, sizeof(s_fh), "Unknown(%ju)", (uintmax_t) fh);
		return (s_fh);
	}
}

static const char *
aeabi_optm_goal(uint64_t og)
{
	static char s_og[32];

	switch (og) {
	case 0: return "None";
	case 1: return "Speed";
	case 2: return "Speed aggressive";
	case 3: return "Space";
	case 4: return "Space aggressive";
	case 5: return "Debugging";
	case 6: return "Best Debugging";
	default:
		snprintf(s_og, sizeof(s_og), "Unknown(%ju)", (uintmax_t) og);
		return (s_og);
	}
}

static const char *
aeabi_fp_optm_goal(uint64_t fog)
{
	static char s_fog[32];

	switch (fog) {
	case 0: return "None";
	case 1: return "Speed";
	case 2: return "Speed aggressive";
	case 3: return "Space";
	case 4: return "Space aggressive";
	case 5: return "Accurary";
	case 6: return "Best Accurary";
	default:
		snprintf(s_fog, sizeof(s_fog), "Unknown(%ju)",
		    (uintmax_t) fog);
		return (s_fog);
	}
}

static const char *
aeabi_virtual(uint64_t vt)
{
	static char s_virtual[64];

	switch (vt) {
	case 0: return "No";
	case 1: return "TrustZone";
	case 2: return "Virtualization extension";
	case 3: return "TrustZone and virtualization extension";
	default:
		snprintf(s_virtual, sizeof(s_virtual), "Unknown(%ju)",
		    (uintmax_t) vt);
		return (s_virtual);
	}
}

static struct {
	uint64_t tag;
	const char *s_tag;
	const char *(*get_desc)(uint64_t val);
} aeabi_tags[] = {
	{4, "Tag_CPU_raw_name", NULL},
	{5, "Tag_CPU_name", NULL},
	{6, "Tag_CPU_arch", aeabi_cpu_arch},
	{7, "Tag_CPU_arch_profile", aeabi_cpu_arch_profile},
	{8, "Tag_ARM_ISA_use", aeabi_arm_isa},
	{9, "Tag_THUMB_ISA_use", aeabi_thumb_isa},
	{10, "Tag_FP_arch", aeabi_fp_arch},
	{11, "Tag_WMMX_arch", aeabi_wmmx_arch},
	{12, "Tag_Advanced_SIMD_arch", aeabi_adv_simd_arch},
	{13, "Tag_PCS_config", aeabi_pcs_config},
	{14, "Tag_ABI_PCS_R9_use", aeabi_pcs_r9},
	{15, "Tag_ABI_PCS_RW_data", aeabi_pcs_rw},
	{16, "Tag_ABI_PCS_RO_data", aeabi_pcs_ro},
	{17, "Tag_ABI_PCS_GOT_use", aeabi_pcs_got},
	{18, "Tag_ABI_PCS_wchar_t", aeabi_pcs_wchar_t},
	{19, "Tag_ABI_FP_rounding", aeabi_fp_rounding},
	{20, "Tag_ABI_FP_denormal", aeabi_fp_denormal},
	{21, "Tag_ABI_FP_exceptions", aeabi_fp_exceptions},
	{22, "Tag_ABI_FP_user_exceptions", aeabi_fp_user_exceptions},
	{23, "Tag_ABI_FP_number_model", aeabi_fp_number_model},
	{24, "Tag_ABI_align_needed", aeabi_align_needed},
	{25, "Tag_ABI_align_preserved", aeabi_align_preserved},
	{26, "Tag_ABI_enum_size", aeabi_enum_size},
	{27, "Tag_ABI_HardFP_use", aeabi_hardfp},
	{28, "Tag_ABI_VFP_args", aeabi_vfp_args},
	{29, "Tag_ABI_WMMX_args", aeabi_wmmx_args},
	{30, "Tag_ABI_optimization_goals", aeabi_optm_goal},
	{31, "Tag_ABI_FP_optimization_goals", aeabi_fp_optm_goal},
	{32, "Tag_compatibility", NULL},
	{34, "Tag_CPU_unaligned_access", aeabi_unaligned_access},
	{36, "Tag_FP_HP_extension", aeabi_fp_hpext},
	{38, "Tag_ABI_FP_16bit_format", aeabi_fp_16bit_format},
	{42, "Tag_MPextension_use", aeabi_mpext},
	{44, "Tag_DIV_use", aeabi_div},
	{64, "Tag_nodefaults", NULL},
	{65, "Tag_also_compatible_with", NULL},
	{66, "Tag_T2EE_use", aeabi_t2ee},
	{67, "Tag_conformance", NULL},
	{68, "Tag_Virtualization_use", aeabi_virtual},
	{70, "Tag_MPextension_use", aeabi_mpext},
};

static const char *
mips_abi_fp(uint64_t fp)
{
	static char s_mips_abi_fp[64];

	switch (fp) {
	case 0: return "N/A";
	case 1: return "Hard float (double precision)";
	case 2: return "Hard float (single precision)";
	case 3: return "Soft float";
	case 4: return "64-bit float (-mips32r2 -mfp64)";
	default:
		snprintf(s_mips_abi_fp, sizeof(s_mips_abi_fp), "Unknown(%ju)",
		    (uintmax_t) fp);
		return (s_mips_abi_fp);
	}
}

static const char *
ppc_abi_fp(uint64_t fp)
{
	static char s_ppc_abi_fp[64];

	switch (fp) {
	case 0: return "N/A";
	case 1: return "Hard float (double precision)";
	case 2: return "Soft float";
	case 3: return "Hard float (single precision)";
	default:
		snprintf(s_ppc_abi_fp, sizeof(s_ppc_abi_fp), "Unknown(%ju)",
		    (uintmax_t) fp);
		return (s_ppc_abi_fp);
	}
}

static const char *
ppc_abi_vector(uint64_t vec)
{
	static char s_vec[64];

	switch (vec) {
	case 0: return "N/A";
	case 1: return "Generic purpose registers";
	case 2: return "AltiVec registers";
	case 3: return "SPE registers";
	default:
		snprintf(s_vec, sizeof(s_vec), "Unknown(%ju)", (uintmax_t) vec);
		return (s_vec);
	}
}

static void
dump_ehdr(struct readcgcef *re)
{
	size_t		 shnum, shstrndx;
	int		 i;

	printf("CGCEF Header:\n");

	/* e_ident[]. */
	printf("  Magic:   ");
	for (i = 0; i < EI_NIDENT; i++)
		printf("%.2x ", re->ehdr.e_ident[i]);
	putchar('\n');

	/* EI_CLASS. */
	printf("%-37s%s\n", "  Class:", cgcef_class(re->ehdr.e_ident[EI_CLASS]));

	/* EI_DATA. */
	printf("%-37s%s\n", "  Data:", cgcef_endian(re->ehdr.e_ident[EI_DATA]));

	/* EI_VERSION. */
	printf("%-37s%d %s\n", "  Version:", re->ehdr.e_ident[EI_VERSION],
	    cgcef_ver(re->ehdr.e_ident[EI_VERSION]));

	/* EI_OSABI. */
	printf("%-37s%s\n", "  OS/ABI:", cgcef_osabi(re->ehdr.e_ident[EI_OSABI]));

	/* EI_ABIVERSION. */
	printf("%-37s%d\n", "  ABI Version:", re->ehdr.e_ident[EI_ABIVERSION]);

	/* e_type. */
	printf("%-37s%s\n", "  Type:", cgcef_type(re->ehdr.e_type));

	/* e_machine. */
	printf("%-37s%s\n", "  Machine:", cgcef_machine(re->ehdr.e_machine));

	/* e_version. */
	printf("%-37s%#x\n", "  Version:", re->ehdr.e_version);

	/* e_entry. */
	printf("%-37s%#jx\n", "  Entry point address:",
	    (uintmax_t)re->ehdr.e_entry);

	/* e_phoff. */
	printf("%-37s%ju (bytes into file)\n", "  Start of program headers:",
	    (uintmax_t)re->ehdr.e_phoff);

	/* e_shoff. */
	printf("%-37s%ju (bytes into file)\n", "  Start of section headers:",
	    (uintmax_t)re->ehdr.e_shoff);

	/* e_flags. */
	printf("%-37s%#x", "  Flags:", re->ehdr.e_flags);
	dump_eflags(re, re->ehdr.e_flags);
	putchar('\n');

	/* e_ehsize. */
	printf("%-37s%u (bytes)\n", "  Size of this header:",
	    re->ehdr.e_ehsize);

	/* e_phentsize. */
	printf("%-37s%u (bytes)\n", "  Size of program headers:",
	    re->ehdr.e_phentsize);

	/* e_phnum. */
	printf("%-37s%u\n", "  Number of program headers:", re->ehdr.e_phnum);

	/* e_shentsize. */
	printf("%-37s%u (bytes)\n", "  Size of section headers:",
	    re->ehdr.e_shentsize);

	/* e_shnum. */
	printf("%-37s%u", "  Number of section headers:", re->ehdr.e_shnum);
	if (re->ehdr.e_shnum == SHN_UNDEF) {
		/* Extended section numbering is in use. */
		if (cgcef_getshdrnum(re->cgcef, &shnum) >= 0)
			printf(" (%ju)", (uintmax_t)shnum);
	}
	putchar('\n');

	/* e_shstrndx. */
	printf("%-37s%u", "  Section header string table index:",
	    re->ehdr.e_shstrndx);
	if (re->ehdr.e_shstrndx == SHN_XINDEX) {
		/* Extended section numbering is in use. */
		if (cgcef_getshdrstrndx(re->cgcef, &shstrndx) >= 0)
			printf(" (%ju)", (uintmax_t)shstrndx);
	}
	putchar('\n');
}

static void
dump_eflags(struct readcgcef *re, uint64_t e_flags)
{
	struct eflags_desc *edesc;
//	int arm_eabi;

	edesc = NULL;
	switch (re->ehdr.e_machine) {
#if 0
	case EM_ARM:
		arm_eabi = (e_flags & EF_ARM_EABIMASK) >> 24;
		if (arm_eabi == 0)
			printf(", GNU EABI");
		else if (arm_eabi <= 5)
			printf(", Version%d EABI", arm_eabi);
		edesc = arm_eflags_desc;
		break;
	case EM_MIPS:
	case EM_MIPS_RS3_LE:
		switch ((e_flags & EF_MIPS_ARCH) >> 28) {
		case 0:	printf(", mips1"); break;
		case 1: printf(", mips2"); break;
		case 2: printf(", mips3"); break;
		case 3: printf(", mips4"); break;
		case 4: printf(", mips5"); break;
		case 5: printf(", mips32"); break;
		case 6: printf(", mips64"); break;
		case 7: printf(", mips32r2"); break;
		case 8: printf(", mips64r2"); break;
		default: break;
		}
		switch ((e_flags & 0x00FF0000) >> 16) {
		case 0x81: printf(", 3900"); break;
		case 0x82: printf(", 4010"); break;
		case 0x83: printf(", 4100"); break;
		case 0x85: printf(", 4650"); break;
		case 0x87: printf(", 4120"); break;
		case 0x88: printf(", 4111"); break;
		case 0x8a: printf(", sb1"); break;
		case 0x8b: printf(", octeon"); break;
		case 0x8c: printf(", xlr"); break;
		case 0x91: printf(", 5400"); break;
		case 0x98: printf(", 5500"); break;
		case 0x99: printf(", 9000"); break;
		case 0xa0: printf(", loongson-2e"); break;
		case 0xa1: printf(", loongson-2f"); break;
		default: break;
		}
		switch ((e_flags & 0x0000F000) >> 12) {
		case 1: printf(", o32"); break;
		case 2: printf(", o64"); break;
		case 3: printf(", eabi32"); break;
		case 4: printf(", eabi64"); break;
		default: break;
		}
		edesc = mips_eflags_desc;
		break;
	case EM_PPC:
	case EM_PPC64:
		edesc = powerpc_eflags_desc;
		break;
	case EM_SPARC:
	case EM_SPARC32PLUS:
	case EM_SPARCV9:
		switch ((e_flags & EF_SPARCV9_MM)) {
		case EF_SPARCV9_TSO: printf(", tso"); break;
		case EF_SPARCV9_PSO: printf(", pso"); break;
		case EF_SPARCV9_MM: printf(", rmo"); break;
		default: break;
		}
		edesc = sparc_eflags_desc;
		break;
#endif /* 0 */
	default:
		break;
	}

	if (edesc != NULL) {
		while (edesc->desc != NULL) {
			if (e_flags & edesc->flag)
				printf(", %s", edesc->desc);
			edesc++;
		}
	}
}

static void
dump_phdr(struct readcgcef *re)
{
	const char	*rawfile;
	GCGCEf_Phdr	 phdr;
	size_t		 phnum;
	int		 i, j;

#define	PH_HDR	"Type", "Offset", "VirtAddr", "PhysAddr", "FileSiz",	\
		"MemSiz", "Flg", "Align"
#define	PH_CT	phdr_type(phdr.p_type), (uintmax_t)phdr.p_offset,	\
		(uintmax_t)phdr.p_vaddr, (uintmax_t)phdr.p_paddr,	\
		(uintmax_t)phdr.p_filesz, (uintmax_t)phdr.p_memsz,	\
		phdr.p_flags & PF_R ? 'R' : ' ',			\
		phdr.p_flags & PF_W ? 'W' : ' ',			\
		phdr.p_flags & PF_X ? 'E' : ' ',			\
		(uintmax_t)phdr.p_align

	if (cgcef_getphdrnum(re->cgcef, &phnum) < 0) {
		warnx("cgcef_getphnum failed: %s", cgcef_errmsg(-1));
		return;
	}
	if (phnum == 0) {
		printf("\nThere are no program headers in this file.\n");
		return;
	}

	printf("\nCGCEf file type is %s", cgcef_type(re->ehdr.e_type));
	printf("\nEntry point 0x%jx\n", (uintmax_t)re->ehdr.e_entry);
	printf("There are %ju program headers, starting at offset %ju\n",
	    (uintmax_t)phnum, (uintmax_t)re->ehdr.e_phoff);

	/* Dump program headers. */
	printf("\nProgram Headers:\n");
	if (re->ec == CGCEFCLASS32)
		printf("  %-15s%-9s%-11s%-11s%-8s%-8s%-4s%s\n", PH_HDR);
	else if (re->options & RE_WW)
		printf("  %-15s%-9s%-19s%-19s%-9s%-9s%-4s%s\n", PH_HDR);
	else
		printf("  %-15s%-19s%-19s%s\n                 %-19s%-20s"
		    "%-7s%s\n", PH_HDR);
	for (i = 0; (size_t) i < phnum; i++) {
		if (gcgcef_getphdr(re->cgcef, i, &phdr) != &phdr) {
			warnx("gcgcef_getphdr failed: %s", cgcef_errmsg(-1));
			continue;
		}
		/* TODO: Add arch-specific segment type dump. */
		if (re->ec == CGCEFCLASS32)
			printf("  %-14.14s 0x%6.6jx 0x%8.8jx 0x%8.8jx "
			    "0x%5.5jx 0x%5.5jx %c%c%c %#jx\n", PH_CT);
		else if (re->options & RE_WW)
			printf("  %-14.14s 0x%6.6jx 0x%16.16jx 0x%16.16jx "
			    "0x%6.6jx 0x%6.6jx %c%c%c %#jx\n", PH_CT);
		else
			printf("  %-14.14s 0x%16.16jx 0x%16.16jx 0x%16.16jx\n"
			    "                 0x%16.16jx 0x%16.16jx  %c%c%c"
			    "    %#jx\n", PH_CT);
		if (phdr.p_type == PT_INTERP) {
			if ((rawfile = cgcef_rawfile(re->cgcef, NULL)) == NULL) {
				warnx("cgcef_rawfile failed: %s", cgcef_errmsg(-1));
				continue;
			}
			printf("      [Requesting program interpreter: %s]\n",
				rawfile + phdr.p_offset);
		}
	}

	/* Dump section to segment mapping. */
	if (re->shnum == 0)
		return;
	printf("\n Section to Segment mapping:\n");
	printf("  Segment Sections...\n");
	for (i = 0; (size_t)i < phnum; i++) {
		if (gcgcef_getphdr(re->cgcef, i, &phdr) != &phdr) {
			warnx("gcgcef_getphdr failed: %s", cgcef_errmsg(-1));
			continue;
		}
		printf("   %2.2d     ", i);
		/* skip NULL section. */
		for (j = 1; (size_t)j < re->shnum; j++)
			if (re->sl[j].off >= phdr.p_offset &&
			    re->sl[j].off + re->sl[j].sz <=
			    phdr.p_offset + phdr.p_memsz)
				printf("%s ", re->sl[j].name);
		printf("\n");
	}
#undef	PH_HDR
#undef	PH_CT
}

static char *
section_flags(struct readcgcef *re, struct section *s)
{
#define BUF_SZ 256
	static char	buf[BUF_SZ];
	int		i, p, nb;

	p = 0;
	nb = re->ec == CGCEFCLASS32 ? 8 : 16;
	if (re->options & RE_T) {
		snprintf(buf, BUF_SZ, "[%*.*jx]: ", nb, nb,
		    (uintmax_t)s->flags);
		p += nb + 4;
	}
	for (i = 0; section_flag[i].ln != NULL; i++) {
		if ((s->flags & section_flag[i].value) == 0)
			continue;
		if (re->options & RE_T) {
			snprintf(&buf[p], BUF_SZ - p, "%s, ",
			    section_flag[i].ln);
			p += strlen(section_flag[i].ln) + 2;
		} else
			buf[p++] = section_flag[i].sn;
	}
	if (re->options & RE_T && p > nb + 4)
		p -= 2;
	buf[p] = '\0';

	return (buf);
}

static void
dump_shdr(struct readcgcef *re)
{
	struct section	*s;
	int		 i;

#define	S_HDR	"[Nr] Name", "Type", "Addr", "Off", "Size", "ES",	\
		"Flg", "Lk", "Inf", "Al"
#define	S_HDRL	"[Nr] Name", "Type", "Address", "Offset", "Size",	\
		"EntSize", "Flags", "Link", "Info", "Align"
#define	ST_HDR	"[Nr] Name", "Type", "Addr", "Off", "Size", "ES",	\
		"Lk", "Inf", "Al", "Flags"
#define	ST_HDRL	"[Nr] Name", "Type", "Address", "Offset", "Link",	\
		"Size", "EntSize", "Info", "Align", "Flags"
#define	S_CT	i, s->name, section_type(re->ehdr.e_machine, s->type),	\
		(uintmax_t)s->addr, (uintmax_t)s->off, (uintmax_t)s->sz,\
		(uintmax_t)s->entsize, section_flags(re, s),		\
		s->link, s->info, (uintmax_t)s->align
#define	ST_CT	i, s->name, section_type(re->ehdr.e_machine, s->type),  \
		(uintmax_t)s->addr, (uintmax_t)s->off, (uintmax_t)s->sz,\
		(uintmax_t)s->entsize, s->link, s->info,		\
		(uintmax_t)s->align, section_flags(re, s)
#define	ST_CTL	i, s->name, section_type(re->ehdr.e_machine, s->type),  \
		(uintmax_t)s->addr, (uintmax_t)s->off, s->link,		\
		(uintmax_t)s->sz, (uintmax_t)s->entsize, s->info,	\
		(uintmax_t)s->align, section_flags(re, s)

	if (re->shnum == 0) {
		printf("\nThere are no sections in this file.\n");
		return;
	}
	printf("There are %ju section headers, starting at offset 0x%jx:\n",
	    (uintmax_t)re->shnum, (uintmax_t)re->ehdr.e_shoff);
	printf("\nSection Headers:\n");
	if (re->ec == CGCEFCLASS32) {
		if (re->options & RE_T)
			printf("  %s\n       %-16s%-9s%-7s%-7s%-5s%-3s%-4s%s\n"
			    "%12s\n", ST_HDR);
		else
			printf("  %-23s%-16s%-9s%-7s%-7s%-3s%-4s%-3s%-4s%s\n",
			    S_HDR);
	} else if (re->options & RE_WW) {
		if (re->options & RE_T)
			printf("  %s\n       %-16s%-17s%-7s%-7s%-5s%-3s%-4s%s\n"
			    "%12s\n", ST_HDR);
		else
			printf("  %-23s%-16s%-17s%-7s%-7s%-3s%-4s%-3s%-4s%s\n",
			    S_HDR);
	} else {
		if (re->options & RE_T)
			printf("  %s\n       %-18s%-17s%-18s%s\n       %-18s"
			    "%-17s%-18s%s\n%12s\n", ST_HDRL);
		else
			printf("  %-23s%-17s%-18s%s\n       %-18s%-17s%-7s%"
			    "-6s%-6s%s\n", S_HDRL);
	}
	for (i = 0; (size_t)i < re->shnum; i++) {
		s = &re->sl[i];
		if (re->ec == CGCEFCLASS32) {
			if (re->options & RE_T)
				printf("  [%2d] %s\n       %-15.15s %8.8jx"
				    " %6.6jx %6.6jx %2.2jx  %2u %3u %2ju\n"
				    "       %s\n", ST_CT);
			else
				printf("  [%2d] %-17.17s %-15.15s %8.8jx"
				    " %6.6jx %6.6jx %2.2jx %3s %2u %3u %2ju\n",
				    S_CT);
		} else if (re->options & RE_WW) {
			if (re->options & RE_T)
				printf("  [%2d] %s\n       %-15.15s %16.16jx"
				    " %6.6jx %6.6jx %2.2jx  %2u %3u %2ju\n"
				    "       %s\n", ST_CT);
			else
				printf("  [%2d] %-17.17s %-15.15s %16.16jx"
				    " %6.6jx %6.6jx %2.2jx %3s %2u %3u %2ju\n",
				    S_CT);
		} else {
			if (re->options & RE_T)
				printf("  [%2d] %s\n       %-15.15s  %16.16jx"
				    "  %16.16jx  %u\n       %16.16jx %16.16jx"
				    "  %-16u  %ju\n       %s\n", ST_CTL);
			else
				printf("  [%2d] %-17.17s %-15.15s  %16.16jx"
				    "  %8.8jx\n       %16.16jx  %16.16jx "
				    "%3s      %2u   %3u     %ju\n", S_CT);
		}
	}
	if ((re->options & RE_T) == 0)
		printf("Key to Flags:\n  W (write), A (alloc),"
		    " X (execute), M (merge), S (strings)\n"
		    "  I (info), L (link order), G (group), x (unknown)\n"
		    "  O (extra OS processing required)"
		    " o (OS specific), p (processor specific)\n");

#undef	S_HDR
#undef	S_HDRL
#undef	ST_HDR
#undef	ST_HDRL
#undef	S_CT
#undef	ST_CT
#undef	ST_CTL
}

static void
dump_dynamic(struct readcgcef *re)
{
	GCGCEf_Dyn	 dyn;
	CGCEf_Data	*d;
	struct section	*s;
	int		 cgceferr, i, is_dynamic, j, jmax, nentries;

	is_dynamic = 0;

	for (i = 0; (size_t)i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->type != SHT_DYNAMIC)
			continue;
		(void) cgcef_errno();
		if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
			cgceferr = cgcef_errno();
			if (cgceferr != 0)
				warnx("cgcef_getdata failed: %s", cgcef_errmsg(-1));
			continue;
		}
		if (d->d_size <= 0)
			continue;

		is_dynamic = 1;

		/* Determine the actual number of table entries. */
		nentries = 0;
		jmax = (int) (s->sz / s->entsize);

		for (j = 0; j < jmax; j++) {
			if (gcgcef_getdyn(d, j, &dyn) != &dyn) {
				warnx("gcgcef_getdyn failed: %s",
				    cgcef_errmsg(-1));
				continue;
			}
			nentries ++;
			if (dyn.d_tag == DT_NULL)
				break;
                }

		printf("\nDynamic section at offset 0x%jx", (uintmax_t)s->off);
		printf(" contains %u entries:\n", nentries);

		if (re->ec == CGCEFCLASS32)
			printf("%5s%12s%28s\n", "Tag", "Type", "Name/Value");
		else
			printf("%5s%20s%28s\n", "Tag", "Type", "Name/Value");

		for (j = 0; j < nentries; j++) {
			if (gcgcef_getdyn(d, j, &dyn) != &dyn)
				continue;
			/* Dump dynamic entry type. */
			if (re->ec == CGCEFCLASS32)
				printf(" 0x%8.8jx", (uintmax_t)dyn.d_tag);
			else
				printf(" 0x%16.16jx", (uintmax_t)dyn.d_tag);
			printf(" %-20s", dt_type(re->ehdr.e_machine,
			    dyn.d_tag));
			/* Dump dynamic entry value. */
			dump_dyn_val(re, &dyn, s->link);
		}
	}

	if (!is_dynamic)
		printf("\nThere is no dynamic section in this file.\n");
}

static char *
timestamp(time_t ti)
{
	static char ts[32];
	struct tm *t;

	t = gmtime(&ti);
	snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02d",
	    t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour,
	    t->tm_min, t->tm_sec);

	return (ts);
}

static const char *
dyn_str(struct readcgcef *re, uint32_t stab, uint64_t d_val)
{
	const char *name;

	if (stab == SHN_UNDEF)
		name = "ERROR";
	else if ((name = cgcef_strptr(re->cgcef, stab, d_val)) == NULL) {
		(void) cgcef_errno(); /* clear error */
		name = "ERROR";
	}

	return (name);
}

static void
dump_arch_dyn_val(struct readcgcef *re, GCGCEf_Dyn *dyn, uint32_t stab)
{
//	const char *name;

	switch (re->ehdr.e_machine) {
#if 0
	case EM_MIPS:
	case EM_MIPS_RS3_LE:
		switch (dyn->d_tag) {
		case DT_MIPS_RLD_VERSION:
		case DT_MIPS_LOCAL_GOTNO:
		case DT_MIPS_CONFLICTNO:
		case DT_MIPS_LIBLISTNO:
		case DT_MIPS_SYMTABNO:
		case DT_MIPS_UNREFEXTNO:
		case DT_MIPS_GOTSYM:
		case DT_MIPS_HIPAGENO:
		case DT_MIPS_DELTA_CLASS_NO:
		case DT_MIPS_DELTA_INSTANCE_NO:
		case DT_MIPS_DELTA_RELOC_NO:
		case DT_MIPS_DELTA_SYM_NO:
		case DT_MIPS_DELTA_CLASSSYM_NO:
		case DT_MIPS_LOCALPAGE_GOTIDX:
		case DT_MIPS_LOCAL_GOTIDX:
		case DT_MIPS_HIDDEN_GOTIDX:
		case DT_MIPS_PROTECTED_GOTIDX:
			printf(" %ju\n", (uintmax_t) dyn->d_un.d_val);
			break;
		case DT_MIPS_ICHECKSUM:
		case DT_MIPS_FLAGS:
		case DT_MIPS_BASE_ADDRESS:
		case DT_MIPS_CONFLICT:
		case DT_MIPS_LIBLIST:
		case DT_MIPS_RLD_MAP:
		case DT_MIPS_DELTA_CLASS:
		case DT_MIPS_DELTA_INSTANCE:
		case DT_MIPS_DELTA_RELOC:
		case DT_MIPS_DELTA_SYM:
		case DT_MIPS_DELTA_CLASSSYM:
		case DT_MIPS_CXX_FLAGS:
		case DT_MIPS_PIXIE_INIT:
		case DT_MIPS_SYMBOL_LIB:
		case DT_MIPS_OPTIONS:
		case DT_MIPS_INTERFACE:
		case DT_MIPS_DYNSTR_ALIGN:
		case DT_MIPS_INTERFACE_SIZE:
		case DT_MIPS_RLD_TEXT_RESOLVE_ADDR:
		case DT_MIPS_COMPACT_SIZE:
		case DT_MIPS_GP_VALUE:
		case DT_MIPS_AUX_DYNAMIC:
		case DT_MIPS_PLTGOT:
		case DT_MIPS_RLD_OBJ_UPDATE:
		case DT_MIPS_RWPLT:
			printf(" 0x%jx\n", (uintmax_t) dyn->d_un.d_val);
			break;
		case DT_MIPS_IVERSION:
		case DT_MIPS_PERF_SUFFIX:
		case DT_AUXILIARY:
		case DT_FILTER:
			name = dyn_str(re, stab, dyn->d_un.d_val);
			printf(" %s\n", name);
			break;
		case DT_MIPS_TIME_STAMP:
			printf(" %s\n", timestamp(dyn->d_un.d_val));
			break;
		}
		break;
#endif /* 0 */
	default:
		printf("\n");
		break;
	}
}

static void
dump_dyn_val(struct readcgcef *re, GCGCEf_Dyn *dyn, uint32_t stab)
{
	const char *name;

	if (dyn->d_tag >= DT_LOPROC && dyn->d_tag <= DT_HIPROC) {
		dump_arch_dyn_val(re, dyn, stab);
		return;
	}

	/* These entry values are index into the string table. */
	name = NULL;
	if (dyn->d_tag == DT_NEEDED || dyn->d_tag == DT_SONAME ||
	    dyn->d_tag == DT_RPATH || dyn->d_tag == DT_RUNPATH)
		name = dyn_str(re, stab, dyn->d_un.d_val);

	switch(dyn->d_tag) {
	case DT_NULL:
	case DT_PLTGOT:
	case DT_HASH:
	case DT_STRTAB:
	case DT_SYMTAB:
	case DT_RELA:
	case DT_INIT:
	case DT_SYMBOLIC:
	case DT_REL:
	case DT_DEBUG:
	case DT_TEXTREL:
	case DT_JMPREL:
	case DT_FINI:
	case DT_VERDEF:
	case DT_VERNEED:
	case DT_VERSYM:
#if 0
	case DT_GNU_HASH:
	case DT_GNU_LIBLIST:
	case DT_GNU_CONFLICT:
		printf(" 0x%jx\n", (uintmax_t) dyn->d_un.d_val);
		break;
#endif /* 0 */
	case DT_PLTRELSZ:
	case DT_RELASZ:
	case DT_RELAENT:
	case DT_STRSZ:
	case DT_SYMENT:
	case DT_RELSZ:
	case DT_RELENT:
	case DT_INIT_ARRAYSZ:
	case DT_FINI_ARRAYSZ:
#if 0
	case DT_GNU_CONFLICTSZ:
	case DT_GNU_LIBLISTSZ:
		printf(" %ju (bytes)\n", (uintmax_t) dyn->d_un.d_val);
		break;
#endif /* 0 */
 	case DT_RELACOUNT:
	case DT_RELCOUNT:
	case DT_VERDEFNUM:
	case DT_VERNEEDNUM:
		printf(" %ju\n", (uintmax_t) dyn->d_un.d_val);
		break;
	case DT_NEEDED:
		printf(" Shared library: [%s]\n", name);
		break;
	case DT_SONAME:
		printf(" Library soname: [%s]\n", name);
		break;
	case DT_RPATH:
		printf(" Library rpath: [%s]\n", name);
		break;
	case DT_RUNPATH:
		printf(" Library runpath: [%s]\n", name);
		break;
	case DT_PLTREL:
		printf(" %s\n", dt_type(re->ehdr.e_machine, dyn->d_un.d_val));
		break;
#if 0
	case DT_GNU_PRELINKED:
		printf(" %s\n", timestamp(dyn->d_un.d_val));
		break;
#endif /* 0 */
	default:
		printf("\n");
	}
}

static void
dump_rel(struct readcgcef *re, struct section *s, CGCEf_Data *d)
{
	GCGCEf_Rel r;
	const char *symname;
	uint64_t symval;
	int i, len;

#define	REL_HDR "r_offset", "r_info", "r_type", "st_value", "st_name"
#define	REL_CT32 (uintmax_t)r.r_offset, (uintmax_t)r.r_info,	    \
		r_type(re->ehdr.e_machine, CGCEF32_R_TYPE(r.r_info)), \
		(uintmax_t)symval, symname
#define	REL_CT64 (uintmax_t)r.r_offset, (uintmax_t)r.r_info,	    \
		r_type(re->ehdr.e_machine, CGCEF64_R_TYPE(r.r_info)), \
		(uintmax_t)symval, symname

	printf("\nRelocation section (%s):\n", s->name);
	if (re->ec == CGCEFCLASS32)
		printf("%-8s %-8s %-19s %-8s %s\n", REL_HDR);
	else {
		if (re->options & RE_WW)
			printf("%-16s %-16s %-24s %-16s %s\n", REL_HDR);
		else
			printf("%-12s %-12s %-19s %-16s %s\n", REL_HDR);
	}
	len = d->d_size / s->entsize;
	for (i = 0; i < len; i++) {
		if (gcgcef_getrel(d, i, &r) != &r) {
			warnx("gcgcef_getrel failed: %s", cgcef_errmsg(-1));
			continue;
		}
		symname = get_symbol_name(re, s->link, GCGCEF_R_SYM(r.r_info));
		symval = get_symbol_value(re, s->link, GCGCEF_R_SYM(r.r_info));
		if (re->ec == CGCEFCLASS32) {
			r.r_info = CGCEF32_R_INFO(CGCEF64_R_SYM(r.r_info),
			    CGCEF64_R_TYPE(r.r_info));
			printf("%8.8jx %8.8jx %-19.19s %8.8jx %s\n", REL_CT32);
		} else {
			if (re->options & RE_WW)
				printf("%16.16jx %16.16jx %-24.24s"
				    " %16.16jx %s\n", REL_CT64);
			else
				printf("%12.12jx %12.12jx %-19.19s"
				    " %16.16jx %s\n", REL_CT64);
		}
	}

#undef	REL_HDR
#undef	REL_CT
}

static void
dump_rela(struct readcgcef *re, struct section *s, CGCEf_Data *d)
{
	GCGCEf_Rela r;
	const char *symname;
	uint64_t symval;
	int i, len;

#define	RELA_HDR "r_offset", "r_info", "r_type", "st_value", \
		"st_name + r_addend"
#define	RELA_CT32 (uintmax_t)r.r_offset, (uintmax_t)r.r_info,	    \
		r_type(re->ehdr.e_machine, CGCEF32_R_TYPE(r.r_info)), \
		(uintmax_t)symval, symname
#define	RELA_CT64 (uintmax_t)r.r_offset, (uintmax_t)r.r_info,	    \
		r_type(re->ehdr.e_machine, CGCEF64_R_TYPE(r.r_info)), \
		(uintmax_t)symval, symname

	printf("\nRelocation section with addend (%s):\n", s->name);
	if (re->ec == CGCEFCLASS32)
		printf("%-8s %-8s %-19s %-8s %s\n", RELA_HDR);
	else {
		if (re->options & RE_WW)
			printf("%-16s %-16s %-24s %-16s %s\n", RELA_HDR);
		else
			printf("%-12s %-12s %-19s %-16s %s\n", RELA_HDR);
	}
	len = d->d_size / s->entsize;
	for (i = 0; i < len; i++) {
		if (gcgcef_getrela(d, i, &r) != &r) {
			warnx("gcgcef_getrel failed: %s", cgcef_errmsg(-1));
			continue;
		}
		symname = get_symbol_name(re, s->link, GCGCEF_R_SYM(r.r_info));
		symval = get_symbol_value(re, s->link, GCGCEF_R_SYM(r.r_info));
		if (re->ec == CGCEFCLASS32) {
			r.r_info = CGCEF32_R_INFO(CGCEF64_R_SYM(r.r_info),
			    CGCEF64_R_TYPE(r.r_info));
			printf("%8.8jx %8.8jx %-19.19s %8.8jx %s", RELA_CT32);
			printf(" + %x\n", (uint32_t) r.r_addend);
		} else {
			if (re->options & RE_WW)
				printf("%16.16jx %16.16jx %-24.24s"
				    " %16.16jx %s", RELA_CT64);
			else
				printf("%12.12jx %12.12jx %-19.19s"
				    " %16.16jx %s", RELA_CT64);
			printf(" + %jx\n", (uintmax_t) r.r_addend);
		}
	}

#undef	RELA_HDR
#undef	RELA_CT
}

static void
dump_reloc(struct readcgcef *re)
{
	struct section *s;
	CGCEf_Data *d;
	int i, cgceferr;

	for (i = 0; (size_t)i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->type == SHT_REL || s->type == SHT_RELA) {
			(void) cgcef_errno();
			if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
				cgceferr = cgcef_errno();
				if (cgceferr != 0)
					warnx("cgcef_getdata failed: %s",
					    cgcef_errmsg(cgceferr));
				continue;
			}
			if (s->type == SHT_REL)
				dump_rel(re, s, d);
			else
				dump_rela(re, s, d);
		}
	}
}

static void
dump_symtab(struct readcgcef *re, int i)
{
	struct section *s;
	CGCEf_Data *d;
	GCGCEf_Sym sym;
	const char *name;
	int cgceferr, stab, j;

	s = &re->sl[i];
	stab = s->link;
	(void) cgcef_errno();
	if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_getdata failed: %s", cgcef_errmsg(cgceferr));
		return;
	}
	if (d->d_size <= 0)
		return;
	printf("Symbol table (%s)", s->name);
	printf(" contains %ju entries:\n", s->sz / s->entsize);
	printf("%7s%9s%14s%5s%8s%6s%9s%5s\n", "Num:", "Value", "Size", "Type",
	    "Bind", "Vis", "Ndx", "Name");

	for (j = 0; (uint64_t)j < s->sz / s->entsize; j++) {
		if (gcgcef_getsym(d, j, &sym) != &sym) {
			warnx("gcgcef_getsym failed: %s", cgcef_errmsg(-1));
			continue;
		}
		printf("%6d:", j);
		printf(" %16.16jx", (uintmax_t)sym.st_value);
		printf(" %5ju", sym.st_size);
		printf(" %-7s", st_type(GCGCEF_ST_TYPE(sym.st_info)));
		printf(" %-6s", st_bind(GCGCEF_ST_BIND(sym.st_info)));
//		printf(" %-8s", st_vis(GCGCEF_ST_VISIBILITY(sym.st_other)));
		printf(" %3s", st_shndx(sym.st_shndx));
		if ((name = cgcef_strptr(re->cgcef, stab, sym.st_name)) != NULL)
			printf(" %s", name);
		/* Append symbol version string for SHT_DYNSYM symbol table. */
		if (s->type == SHT_DYNSYM && re->ver != NULL &&
		    re->vs != NULL && re->vs[j] > 1) {
			if (re->vs[j] & 0x8000 ||
			    re->ver[re->vs[j] & 0x7fff].type == 0)
				printf("@%s (%d)",
				    re->ver[re->vs[j] & 0x7fff].name,
				    re->vs[j] & 0x7fff);
			else
				printf("@@%s (%d)", re->ver[re->vs[j]].name,
				    re->vs[j]);
		}
		putchar('\n');
	}

}

static void
dump_symtabs(struct readcgcef *re)
{
	GCGCEf_Dyn dyn;
	CGCEf_Data *d;
	struct section *s;
	uint64_t dyn_off;
	int cgceferr, i;

	/*
	 * If -D is specified, only dump the symbol table specified by
	 * the DT_SYMTAB entry in the .dynamic section.
	 */
	dyn_off = 0;
	if (re->options & RE_DD) {
		s = NULL;
		for (i = 0; (size_t)i < re->shnum; i++)
			if (re->sl[i].type == SHT_DYNAMIC) {
				s = &re->sl[i];
				break;
			}
		if (s == NULL)
			return;
		(void) cgcef_errno();
		if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
			cgceferr = cgcef_errno();
			if (cgceferr != 0)
				warnx("cgcef_getdata failed: %s", cgcef_errmsg(-1));
			return;
		}
		if (d->d_size <= 0)
			return;

		for (i = 0; (uint64_t)i < s->sz / s->entsize; i++) {
			if (gcgcef_getdyn(d, i, &dyn) != &dyn) {
				warnx("gcgcef_getdyn failed: %s", cgcef_errmsg(-1));
				continue;
			}
			if (dyn.d_tag == DT_SYMTAB) {
				dyn_off = dyn.d_un.d_val;
				break;
			}
		}
	}

	/* Find and dump symbol tables. */
	for (i = 0; (size_t)i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->type == SHT_SYMTAB || s->type == SHT_DYNSYM) {
			if (re->options & RE_DD) {
				if (dyn_off == s->addr) {
					dump_symtab(re, i);
					break;
				}
			} else
				dump_symtab(re, i);
		}
	}
}

static void
dump_svr4_hash(struct section *s)
{
	CGCEf_Data	*d;
	uint32_t	*buf;
	uint32_t	 nbucket, nchain;
	uint32_t	*bucket, *chain;
	uint32_t	*bl, *c, maxl, total;
	int		 cgceferr, i, j;

	/* Read and parse the content of .hash section. */
	(void) cgcef_errno();
	if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_getdata failed: %s", cgcef_errmsg(cgceferr));
		return;
	}
	if (d->d_size < 2 * sizeof(uint32_t)) {
		warnx(".hash section too small");
		return;
	}
	buf = d->d_buf;
	nbucket = buf[0];
	nchain = buf[1];
	if (nbucket <= 0 || nchain <= 0) {
		warnx("Malformed .hash section");
		return;
	}
	if (d->d_size != (nbucket + nchain + 2) * sizeof(uint32_t)) {
		warnx("Malformed .hash section");
		return;
	}
	bucket = &buf[2];
	chain = &buf[2 + nbucket];

	maxl = 0;
	if ((bl = calloc(nbucket, sizeof(*bl))) == NULL)
		errx(EXIT_FAILURE, "calloc failed");
	for (i = 0; (uint32_t)i < nbucket; i++)
		for (j = bucket[i]; j > 0 && (uint32_t)j < nchain; j = chain[j])
			if (++bl[i] > maxl)
				maxl = bl[i];
	if ((c = calloc(maxl + 1, sizeof(*c))) == NULL)
		errx(EXIT_FAILURE, "calloc failed");
	for (i = 0; (uint32_t)i < nbucket; i++)
		c[bl[i]]++;
	printf("\nHistogram for bucket list length (total of %u buckets):\n",
	    nbucket);
	printf(" Length\tNumber\t\t%% of total\tCoverage\n");
	total = 0;
	for (i = 0; (uint32_t)i <= maxl; i++) {
		total += c[i] * i;
		printf("%7u\t%-10u\t(%5.1f%%)\t%5.1f%%\n", i, c[i],
		    c[i] * 100.0 / nbucket, total * 100.0 / (nchain - 1));
	}
	free(c);
	free(bl);
}

static void
dump_svr4_hash64(struct readcgcef *re, struct section *s)
{
	CGCEf_Data	*d, dst;
	uint64_t	*buf;
	uint64_t	 nbucket, nchain;
	uint64_t	*bucket, *chain;
	uint64_t	*bl, *c, maxl, total;
	int		 cgceferr, i, j;

	/*
	 * ALPHA uses 64-bit hash entries. Since libcgcef assumes that
	 * .hash section contains only 32-bit entry, an explicit
	 * gcgcef_xlatetom is needed here.
	 */
	(void) cgcef_errno();
	if ((d = cgcef_rawdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_rawdata failed: %s",
			    cgcef_errmsg(cgceferr));
		return;
	}
	d->d_type = CGCEF_T_XWORD;
	memcpy(&dst, d, sizeof(CGCEf_Data));
	if (gcgcef_xlatetom(re->cgcef, &dst, d,
		re->ehdr.e_ident[EI_DATA]) != &dst) {
		warnx("gcgcef_xlatetom failed: %s", cgcef_errmsg(-1));
		return;
	}
	if (dst.d_size < 2 * sizeof(uint64_t)) {
		warnx(".hash section too small");
		return;
	}
	buf = dst.d_buf;
	nbucket = buf[0];
	nchain = buf[1];
	if (nbucket <= 0 || nchain <= 0) {
		warnx("Malformed .hash section");
		return;
	}
	if (d->d_size != (nbucket + nchain + 2) * sizeof(uint32_t)) {
		warnx("Malformed .hash section");
		return;
	}
	bucket = &buf[2];
	chain = &buf[2 + nbucket];

	maxl = 0;
	if ((bl = calloc(nbucket, sizeof(*bl))) == NULL)
		errx(EXIT_FAILURE, "calloc failed");
	for (i = 0; (uint32_t)i < nbucket; i++)
		for (j = bucket[i]; j > 0 && (uint32_t)j < nchain; j = chain[j])
			if (++bl[i] > maxl)
				maxl = bl[i];
	if ((c = calloc(maxl + 1, sizeof(*c))) == NULL)
		errx(EXIT_FAILURE, "calloc failed");
	for (i = 0; (uint64_t)i < nbucket; i++)
		c[bl[i]]++;
	printf("Histogram for bucket list length (total of %ju buckets):\n",
	    (uintmax_t)nbucket);
	printf(" Length\tNumber\t\t%% of total\tCoverage\n");
	total = 0;
	for (i = 0; (uint64_t)i <= maxl; i++) {
		total += c[i] * i;
		printf("%7u\t%-10ju\t(%5.1f%%)\t%5.1f%%\n", i, (uintmax_t)c[i],
		    c[i] * 100.0 / nbucket, total * 100.0 / (nchain - 1));
	}
	free(c);
	free(bl);
}

static void
dump_gnu_hash(struct readcgcef *re, struct section *s)
{
	struct section	*ds;
	CGCEf_Data	*d;
	uint32_t	*buf;
	uint32_t	*bucket, *chain;
	uint32_t	 nbucket, nchain, symndx, maskwords, shift2;
	uint32_t	*bl, *c, maxl, total;
	int		 cgceferr, dynsymcount, i, j;

	(void) cgcef_errno();
	if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_getdata failed: %s",
			    cgcef_errmsg(cgceferr));
		return;
	}
	if (d->d_size < 4 * sizeof(uint32_t)) {
		warnx(".gnu.hash section too small");
		return;
	}
	buf = d->d_buf;
	nbucket = buf[0];
	symndx = buf[1];
	maskwords = buf[2];
	shift2 = buf[3];
	buf += 4;
	ds = &re->sl[s->link];
	dynsymcount = ds->sz / ds->entsize;
	nchain = dynsymcount - symndx;
	if (d->d_size != 4 * sizeof(uint32_t) + maskwords *
	    (re->ec == CGCEFCLASS32 ? sizeof(uint32_t) : sizeof(uint64_t)) +
	    (nbucket + nchain) * sizeof(uint32_t)) {
		warnx("Malformed .gnu.hash section");
		return;
	}
	bucket = buf + (re->ec == CGCEFCLASS32 ? maskwords : maskwords * 2);
	chain = bucket + nbucket;

	maxl = 0;
	if ((bl = calloc(nbucket, sizeof(*bl))) == NULL)
		errx(EXIT_FAILURE, "calloc failed");
	for (i = 0; (uint32_t)i < nbucket; i++)
		for (j = bucket[i]; j > 0 && (uint32_t)j - symndx < nchain;
		     j++) {
			if (++bl[i] > maxl)
				maxl = bl[i];
			if (chain[j - symndx] & 1)
				break;
		}
	if ((c = calloc(maxl + 1, sizeof(*c))) == NULL)
		errx(EXIT_FAILURE, "calloc failed");
	for (i = 0; (uint32_t)i < nbucket; i++)
		c[bl[i]]++;
	printf("Histogram for bucket list length (total of %u buckets):\n",
	    nbucket);
	printf(" Length\tNumber\t\t%% of total\tCoverage\n");
	total = 0;
	for (i = 0; (uint32_t)i <= maxl; i++) {
		total += c[i] * i;
		printf("%7u\t%-10u\t(%5.1f%%)\t%5.1f%%\n", i, c[i],
		    c[i] * 100.0 / nbucket, total * 100.0 / (nchain - 1));
	}
	free(c);
	free(bl);
}

#if 0
static void
dump_hash(struct readcgcef *re)
{
	struct section	*s;
	int		 i;

	for (i = 0; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->type == SHT_HASH || s->type == SHT_GNU_HASH) {
			if (s->type == SHT_GNU_HASH)
				dump_gnu_hash(re, s);
			else if (re->ehdr.e_machine == EM_ALPHA &&
			    s->entsize == 8)
				dump_svr4_hash64(re, s);
			else
				dump_svr4_hash(s);
		}
	}
}
#endif

#if 0
static void
dump_notes(struct readcgcef *re)
{
	struct section *s;
	const char *rawfile;
	GCGCEf_Phdr phdr;
	CGCEf_Data *d;
	size_t phnum;
	int i, cgceferr;

	if (re->ehdr.e_type == ET_CORE) {
		/*
		 * Search program headers in the core file for
		 * PT_NOTE entry.
		 */
		if (cgcef_getphdrnum(re->cgcef, &phnum) < 0) {
			warnx("cgcef_getphnum failed: %s", cgcef_errmsg(-1));
			return;
		}
		if (phnum == 0)
			return;
		if ((rawfile = cgcef_rawfile(re->cgcef, NULL)) == NULL) {
			warnx("cgcef_rawfile failed: %s", cgcef_errmsg(-1));
			return;
		}
		for (i = 0; (size_t) i < phnum; i++) {
			if (gcgcef_getphdr(re->cgcef, i, &phdr) != &phdr) {
				warnx("gcgcef_getphdr failed: %s",
				    cgcef_errmsg(-1));
				continue;
			}
			if (phdr.p_type == PT_NOTE)
				dump_notes_content(re, rawfile + phdr.p_offset,
				    phdr.p_filesz, phdr.p_offset);
		}

	} else {
		/*
		 * For objects other than core files, Search for
		 * SHT_NOTE sections.
		 */
		for (i = 0; (size_t) i < re->shnum; i++) {
			s = &re->sl[i];
			if (s->type == SHT_NOTE) {
				(void) cgcef_errno();
				if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
					cgceferr = cgcef_errno();
					if (cgceferr != 0)
						warnx("cgcef_getdata failed: %s",
						    cgcef_errmsg(cgceferr));
					continue;
				}
				dump_notes_content(re, d->d_buf, d->d_size,
				    s->off);
			}
		}
	}
}

static void
dump_notes_content(struct readcgcef *re, const char *buf, size_t sz, off_t off)
{
	CGCEf_Note *note;
	const char *end;

	printf("\nNotes at offset %#010jx with length %#010jx:\n",
	    (uintmax_t) off, (uintmax_t) sz);
	printf("  %-13s %-15s %s\n", "Owner", "Data size", "Description");
	end = buf + sz;
	while (buf < end) {
		note = (CGCEf_Note *)(uintptr_t) buf;
		printf("  %-13s %#010jx", (char *)(uintptr_t) (note + 1),
		    (uintmax_t) note->n_descsz);
		printf("      %s\n", note_type(re->ehdr.e_ident[EI_OSABI],
		    re->ehdr.e_type, note->n_type));
		buf += sizeof(CGCEf_Note);
		if (re->ec == CGCEFCLASS32)
			buf += roundup2(note->n_namesz, 4) +
			    roundup2(note->n_descsz, 4);
		else
			buf += roundup2(note->n_namesz, 8) +
			    roundup2(note->n_descsz, 8);
	}
}
#endif /* 0 */

/*
 * Symbol versioning sections are the same for 32bit and 64bit
 * CGCEF objects.
 */
#define CGCEf_Verdef	CGCEf32_Verdef
#define	CGCEf_Verdaux	CGCEf32_Verdaux
#define	CGCEf_Verneed	CGCEf32_Verneed
#define	CGCEf_Vernaux	CGCEf32_Vernaux

#define	SAVE_VERSION_NAME(x, n, t)					\
	do {								\
		while (x >= re->ver_sz) {				\
			nv = realloc(re->ver,				\
			    sizeof(*re->ver) * re->ver_sz * 2);		\
			if (nv == NULL) {				\
				warn("realloc failed");			\
				free(re->ver);				\
				return;					\
			}						\
			re->ver = nv;					\
			for (i = re->ver_sz; i < re->ver_sz * 2; i++) {	\
				re->ver[i].name = NULL;			\
				re->ver[i].type = 0;			\
			}						\
			re->ver_sz *= 2;				\
		}							\
		if (x > 1) {						\
			re->ver[x].name = n;				\
			re->ver[x].type = t;				\
		}							\
	} while (0)


static void
dump_verdef(struct readcgcef *re, int dump)
{
	struct section *s;
	struct symver *nv;
	CGCEf_Data *d;
	CGCEf_Verdef *vd;
	CGCEf_Verdaux *vda;
	uint8_t *buf, *end, *buf2;
	const char *name;
	int cgceferr, i, j;

	if ((s = re->vd_s) == NULL)
		return;

	if (re->ver == NULL) {
		re->ver_sz = 16;
		if ((re->ver = calloc(re->ver_sz, sizeof(*re->ver))) ==
		    NULL) {
			warn("calloc failed");
			return;
		}
		re->ver[0].name = "*local*";
		re->ver[1].name = "*global*";
	}

	if (dump)
		printf("\nVersion definition section (%s):\n", s->name);
	(void) cgcef_errno();
	if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_getdata failed: %s", cgcef_errmsg(cgceferr));
		return;
	}
	if (d->d_size == 0)
		return;

	buf = d->d_buf;
	end = buf + d->d_size;
	while (buf + sizeof(CGCEf_Verdef) <= end) {
		vd = (CGCEf_Verdef *) (uintptr_t) buf;
		if (dump) {
			printf("  0x%4.4lx", (unsigned long)
			    (buf - (uint8_t *)d->d_buf));
			printf(" vd_version: %u vd_flags: %d"
			    " vd_ndx: %u vd_cnt: %u", vd->vd_version,
			    vd->vd_flags, vd->vd_ndx, vd->vd_cnt);
		}
		buf2 = buf + vd->vd_aux;
		j = 0;
		while (buf2 + sizeof(CGCEf_Verdaux) <= end && j < vd->vd_cnt) {
			vda = (CGCEf_Verdaux *) (uintptr_t) buf2;
			name = get_string(re, s->link, vda->vda_name);
			if (j == 0) {
				if (dump)
					printf(" vda_name: %s\n", name);
				SAVE_VERSION_NAME((int)vd->vd_ndx, name, 1);
			} else if (dump)
				printf("  0x%4.4lx parent: %s\n",
				    (unsigned long) (buf2 -
				    (uint8_t *)d->d_buf), name);
			if (vda->vda_next == 0)
				break;
			buf2 += vda->vda_next;
			j++;
		}
		if (vd->vd_next == 0)
			break;
		buf += vd->vd_next;
	}
}

static void
dump_verneed(struct readcgcef *re, int dump)
{
	struct section *s;
	struct symver *nv;
	CGCEf_Data *d;
	CGCEf_Verneed *vn;
	CGCEf_Vernaux *vna;
	uint8_t *buf, *end, *buf2;
	const char *name;
	int cgceferr, i, j;

	if ((s = re->vn_s) == NULL)
		return;

	if (re->ver == NULL) {
		re->ver_sz = 16;
		if ((re->ver = calloc(re->ver_sz, sizeof(*re->ver))) ==
		    NULL) {
			warn("calloc failed");
			return;
		}
		re->ver[0].name = "*local*";
		re->ver[1].name = "*global*";
	}

	if (dump)
		printf("\nVersion needed section (%s):\n", s->name);
	(void) cgcef_errno();
	if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_getdata failed: %s", cgcef_errmsg(cgceferr));
		return;
	}
	if (d->d_size == 0)
		return;

	buf = d->d_buf;
	end = buf + d->d_size;
	while (buf + sizeof(CGCEf_Verneed) <= end) {
		vn = (CGCEf_Verneed *) (uintptr_t) buf;
		if (dump) {
			printf("  0x%4.4lx", (unsigned long)
			    (buf - (uint8_t *)d->d_buf));
			printf(" vn_version: %u vn_file: %s vn_cnt: %u\n",
			    vn->vn_version,
			    get_string(re, s->link, vn->vn_file),
			    vn->vn_cnt);
		}
		buf2 = buf + vn->vn_aux;
		j = 0;
		while (buf2 + sizeof(CGCEf_Vernaux) <= end && j < vn->vn_cnt) {
			vna = (CGCEf32_Vernaux *) (uintptr_t) buf2;
			if (dump)
				printf("  0x%4.4lx", (unsigned long)
				    (buf2 - (uint8_t *)d->d_buf));
			name = get_string(re, s->link, vna->vna_name);
			if (dump)
				printf("   vna_name: %s vna_flags: %u"
				    " vna_other: %u\n", name,
				    vna->vna_flags, vna->vna_other);
			SAVE_VERSION_NAME((int)vna->vna_other, name, 0);
			if (vna->vna_next == 0)
				break;
			buf2 += vna->vna_next;
			j++;
		}
		if (vn->vn_next == 0)
			break;
		buf += vn->vn_next;
	}
}

static void
dump_versym(struct readcgcef *re)
{
	int i;

	if (re->vs_s == NULL || re->ver == NULL || re->vs == NULL)
		return;
	printf("\nVersion symbol section (%s):\n", re->vs_s->name);
	for (i = 0; i < re->vs_sz; i++) {
		if ((i & 3) == 0) {
			if (i > 0)
				putchar('\n');
			printf("  %03x:", i);
		}
		if (re->vs[i] & 0x8000)
			printf(" %3xh %-12s ", re->vs[i] & 0x7fff,
			    re->ver[re->vs[i] & 0x7fff].name);
		else
			printf(" %3x %-12s ", re->vs[i],
			    re->ver[re->vs[i]].name);
	}
	putchar('\n');
}

static void
dump_ver(struct readcgcef *re)
{

	if (re->vs_s && re->ver && re->vs)
		dump_versym(re);
	if (re->vd_s)
		dump_verdef(re, 1);
	if (re->vn_s)
		dump_verneed(re, 1);
}

static void
search_ver(struct readcgcef *re)
{
	struct section *s;
	CGCEf_Data *d;
	int cgceferr, i;

	for (i = 0; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->type == SHT_SUNW_versym)
			re->vs_s = s;
		if (s->type == SHT_SUNW_verneed)
			re->vn_s = s;
		if (s->type == SHT_SUNW_verdef)
			re->vd_s = s;
	}
	if (re->vd_s)
		dump_verdef(re, 0);
	if (re->vn_s)
		dump_verneed(re, 0);
	if (re->vs_s && re->ver != NULL) {
		(void) cgcef_errno();
		if ((d = cgcef_getdata(re->vs_s->scn, NULL)) == NULL) {
			cgceferr = cgcef_errno();
			if (cgceferr != 0)
				warnx("cgcef_getdata failed: %s",
				    cgcef_errmsg(cgceferr));
			return;
		}
		if (d->d_size == 0)
			return;
		re->vs = d->d_buf;
		re->vs_sz = d->d_size / sizeof(CGCEf32_Half);
	}
}

#undef	CGCEf_Verdef
#undef	CGCEf_Verdaux
#undef	CGCEf_Verneed
#undef	CGCEf_Vernaux
#undef	SAVE_VERSION_NAME

#if 0
/*
 * CGCEf32_Lib and CGCEf64_Lib are identical.
 */
#define	CGCEf_Lib		CGCEf32_Lib

static void
dump_liblist(struct readcgcef *re)
{
	struct section *s;
	struct tm *t;
	time_t ti;
	char tbuf[20];
	CGCEf_Data *d;
	CGCEf_Lib *lib;
	int i, j, k, cgceferr, first;

	for (i = 0; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->type != SHT_GNU_LIBLIST)
			continue;
		(void) cgcef_errno();
		if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
			cgceferr = cgcef_errno();
			if (cgceferr != 0)
				warnx("cgcef_getdata failed: %s",
				    cgcef_errmsg(cgceferr));
			continue;
		}
		if (d->d_size <= 0)
			continue;
		lib = d->d_buf;
		printf("\nLibrary list section '%s' ", s->name);
		printf("contains %ju entries:\n", s->sz / s->entsize);
		printf("%12s%24s%18s%10s%6s\n", "Library", "Time Stamp",
		    "Checksum", "Version", "Flags");
		for (j = 0; (uint64_t) j < s->sz / s->entsize; j++) {
			printf("%3d: ", j);
			printf("%-20.20s ",
			    get_string(re, s->link, lib->l_name));
			ti = lib->l_time_stamp;
			t = gmtime(&ti);
			snprintf(tbuf, sizeof(tbuf), "%04d-%02d-%02dT%02d:%02d"
			    ":%2d", t->tm_year + 1900, t->tm_mon + 1,
			    t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
			printf("%-19.19s ", tbuf);
			printf("0x%08x ", lib->l_checksum);
			printf("%-7d %#x", lib->l_version, lib->l_flags);
			if (lib->l_flags != 0) {
				first = 1;
				putchar('(');
				for (k = 0; l_flag[k].name != NULL; k++) {
					if ((l_flag[k].value & lib->l_flags) ==
					    0)
						continue;
					if (!first)
						putchar(',');
					else
						first = 0;
					printf("%s", l_flag[k].name);
				}
				putchar(')');
			}
			putchar('\n');
			lib++;
		}
	}
}

#undef CGCEf_Lib
#endif

static uint8_t *
dump_unknown_tag(uint64_t tag, uint8_t *p)
{
	uint64_t val;

	/*
	 * According to ARM EABI: For tags > 32, even numbered tags have
	 * a ULEB128 param and odd numbered ones have NUL-terminated
	 * string param. This rule probably also applies for tags <= 32
	 * if the object arch is not ARM.
	 */

	printf("  Tag_unknown_%ju: ", (uintmax_t) tag);

	if (tag & 1) {
		printf("%s\n", (char *) p);
		p += strlen((char *) p) + 1;
	} else {
		val = _decode_uleb128(&p);
		printf("%ju\n", (uintmax_t) val);
	}

	return (p);
}

static uint8_t *
dump_compatibility_tag(uint8_t *p)
{
	uint64_t val;

	val = _decode_uleb128(&p);
	printf("flag = %ju, vendor = %s\n", val, p);
	p += strlen((char *) p) + 1;

	return (p);
}

static void
dump_arm_attributes(struct readcgcef *re, uint8_t *p, uint8_t *pe)
{
	uint64_t tag, val;
	size_t i;
	int found, desc;

	(void) re;

	while (p < pe) {
		tag = _decode_uleb128(&p);
		found = desc = 0;
		for (i = 0; i < sizeof(aeabi_tags) / sizeof(aeabi_tags[0]);
		     i++) {
			if (tag == aeabi_tags[i].tag) {
				found = 1;
				printf("  %s: ", aeabi_tags[i].s_tag);
				if (aeabi_tags[i].get_desc) {
					desc = 1;
					val = _decode_uleb128(&p);
					printf("%s\n",
					    aeabi_tags[i].get_desc(val));
				}
				break;
			}
			if (tag < aeabi_tags[i].tag)
				break;
		}
		if (!found) {
			p = dump_unknown_tag(tag, p);
			continue;
		}
		if (desc)
			continue;

		switch (tag) {
		case 4:		/* Tag_CPU_raw_name */
		case 5:		/* Tag_CPU_name */
		case 67:	/* Tag_conformance */
			printf("%s\n", (char *) p);
			p += strlen((char *) p) + 1;
			break;
		case 32:	/* Tag_compatibility */
			p = dump_compatibility_tag(p);
			break;
		case 64:	/* Tag_nodefaults */
			/* ignored, written as 0. */
			(void) _decode_uleb128(&p);
			printf("True\n");
			break;
		case 65:	/* Tag_also_compatible_with */
			val = _decode_uleb128(&p);
			/* Must be Tag_CPU_arch */
			if (val != 6) {
				printf("unknown\n");
				break;
			}
			val = _decode_uleb128(&p);
			printf("%s\n", aeabi_cpu_arch(val));
			/* Skip NUL terminator. */
			p++;
			break;
		default:
			putchar('\n');
			break;
		}
	}
}

#ifndef	Tag_GNU_MIPS_ABI_FP
#define	Tag_GNU_MIPS_ABI_FP	4
#endif

#if 0
static void
dump_mips_attributes(struct readcgcef *re, uint8_t *p, uint8_t *pe)
{
	uint64_t tag, val;

	(void) re;

	while (p < pe) {
		tag = _decode_uleb128(&p);
		switch (tag) {
		case Tag_GNU_MIPS_ABI_FP:
			val = _decode_uleb128(&p);
			printf("  Tag_GNU_MIPS_ABI_FP: %s\n", mips_abi_fp(val));
			break;
		case 32:	/* Tag_compatibility */
			p = dump_compatibility_tag(p);
			break;
		default:
			p = dump_unknown_tag(tag, p);
			break;
		}
	}
}
#endif

#ifndef Tag_GNU_Power_ABI_FP
#define	Tag_GNU_Power_ABI_FP	4
#endif

#ifndef Tag_GNU_Power_ABI_Vector
#define	Tag_GNU_Power_ABI_Vector	8
#endif

static void
dump_ppc_attributes(uint8_t *p, uint8_t *pe)
{
	uint64_t tag, val;

	while (p < pe) {
		tag = _decode_uleb128(&p);
		switch (tag) {
		case Tag_GNU_Power_ABI_FP:
			val = _decode_uleb128(&p);
			printf("  Tag_GNU_Power_ABI_FP: %s\n", ppc_abi_fp(val));
			break;
		case Tag_GNU_Power_ABI_Vector:
			val = _decode_uleb128(&p);
			printf("  Tag_GNU_Power_ABI_Vector: %s\n",
			    ppc_abi_vector(val));
			break;
		case 32:	/* Tag_compatibility */
			p = dump_compatibility_tag(p);
			break;
		default:
			p = dump_unknown_tag(tag, p);
			break;
		}
	}
}

#if 0
static void
dump_attributes(struct readcgcef *re)
{
	struct section *s;
	CGCEf_Data *d;
	uint8_t *p, *sp;
	size_t len, seclen, nlen, sublen;
	uint64_t val;
	int tag, i, cgceferr;

	for (i = 0; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->type != SHT_GNU_ATTRIBUTES &&
		    (re->ehdr.e_machine != EM_ARM || s->type != SHT_LOPROC + 3))
			continue;
		(void) cgcef_errno();
		if ((d = cgcef_rawdata(s->scn, NULL)) == NULL) {
			cgceferr = cgcef_errno();
			if (cgceferr != 0)
				warnx("cgcef_rawdata failed: %s",
				    cgcef_errmsg(cgceferr));
			continue;
		}
		if (d->d_size <= 0)
			continue;
		p = d->d_buf;
		if (*p != 'A') {
			printf("Unknown Attribute Section Format: %c\n",
			    (char) *p);
			continue;
		}
		len = d->d_size - 1;
		p++;
		while (len > 0) {
			seclen = re->dw_decode(&p, 4);
			if (seclen > len) {
				warnx("invalid attribute section length");
				break;
			}
			len -= seclen;
			printf("Attribute Section: %s\n", (char *) p);
			nlen = strlen((char *) p) + 1;
			p += nlen;
			seclen -= nlen + 4;
			while (seclen > 0) {
				sp = p;
				tag = *p++;
				sublen = re->dw_decode(&p, 4);
				if (sublen > seclen) {
					warnx("invalid attribute sub-section"
					    " length");
					break;
				}
				seclen -= sublen;
				printf("%s", top_tag(tag));
				if (tag == 2 || tag == 3) {
					putchar(':');
					for (;;) {
						val = _decode_uleb128(&p);
						if (val == 0)
							break;
						printf(" %ju", (uintmax_t) val);
					}
				}
				putchar('\n');
				if (re->ehdr.e_machine == EM_ARM &&
				    s->type == SHT_LOPROC + 3)
					dump_arm_attributes(re, p, sp + sublen);
				else if (re->ehdr.e_machine == EM_MIPS ||
				    re->ehdr.e_machine == EM_MIPS_RS3_LE)
					dump_mips_attributes(re, p,
					    sp + sublen);
				else if (re->ehdr.e_machine == EM_PPC)
					dump_ppc_attributes(p, sp + sublen);
				p = sp + sublen;
			}
		}
	}
}
#endif

#if 0
static void
dump_mips_specific_info(struct readcgcef *re)
{
	struct section *s;
	int i, options_found;

	options_found = 0;
	s = NULL;
	for (i = 0; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->name != NULL && (!strcmp(s->name, ".MIPS.options") ||
		    (s->type == SHT_MIPS_OPTIONS))) {
			dump_mips_options(re, s);
			options_found = 1;
		}
	}

	/*
	 * According to SGI mips64 spec, .reginfo should be ignored if
	 * .MIPS.options section is present.
	 */
	if (!options_found) {
		for (i = 0; (size_t) i < re->shnum; i++) {
			s = &re->sl[i];
			if (s->name != NULL && (!strcmp(s->name, ".reginfo") ||
			    (s->type == SHT_MIPS_REGINFO)))
				dump_mips_reginfo(re, s);
		}
	}
}

static void
dump_mips_reginfo(struct readcgcef *re, struct section *s)
{
	CGCEf_Data *d;
	int cgceferr;

	(void) cgcef_errno();
	if ((d = cgcef_rawdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_rawdata failed: %s",
			    cgcef_errmsg(cgceferr));
		return;
	}
	if (d->d_size <= 0)
		return;

	printf("\nSection '%s' contains %ju entries:\n", s->name,
	    s->sz / s->entsize);
	dump_mips_odk_reginfo(re, d->d_buf, d->d_size);
}

static void
dump_mips_options(struct readcgcef *re, struct section *s)
{
	CGCEf_Data *d;
	uint32_t info;
	uint16_t sndx;
	uint8_t *p, *pe;
	uint8_t kind, size;
	int cgceferr;

	(void) cgcef_errno();
	if ((d = cgcef_rawdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_rawdata failed: %s",
			    cgcef_errmsg(cgceferr));
		return;
	}
	if (d->d_size == 0)
		return;

	printf("\nSection %s contains:\n", s->name);
	p = d->d_buf;
	pe = p + d->d_size;
	while (p < pe) {
		kind = re->dw_decode(&p, 1);
		size = re->dw_decode(&p, 1);
		sndx = re->dw_decode(&p, 2);
		info = re->dw_decode(&p, 4);
		switch (kind) {
		case ODK_REGINFO:
			dump_mips_odk_reginfo(re, p, size - 8);
			break;
		case ODK_EXCEPTIONS:
			printf(" EXCEPTIONS FPU_MIN: %#x\n",
			    info & OEX_FPU_MIN);
			printf("%11.11s FPU_MAX: %#x\n", "",
			    info & OEX_FPU_MAX);
			dump_mips_option_flags("", mips_exceptions_option,
			    info);
			break;
		case ODK_PAD:
			printf(" %-10.10s section: %ju\n", "OPAD",
			    (uintmax_t) sndx);
			dump_mips_option_flags("", mips_pad_option, info);
			break;
		case ODK_HWPATCH:
			dump_mips_option_flags("HWPATCH", mips_hwpatch_option,
			    info);
			break;
		case ODK_HWAND:
			dump_mips_option_flags("HWAND", mips_hwa_option, info);
			break;
		case ODK_HWOR:
			dump_mips_option_flags("HWOR", mips_hwo_option, info);
			break;
		case ODK_FILL:
			printf(" %-10.10s %#jx\n", "FILL", (uintmax_t) info);
			break;
		case ODK_TAGS:
			printf(" %-10.10s\n", "TAGS");
			break;
		case ODK_GP_GROUP:
			printf(" %-10.10s GP group number: %#x\n", "GP_GROUP",
			    info & 0xFFFF);
			if (info & 0x10000)
				printf(" %-10.10s GP group is "
				    "scgcef-contained\n", "");
			break;
		case ODK_IDENT:
			printf(" %-10.10s default GP group number: %#x\n",
			    "IDENT", info & 0xFFFF);
			if (info & 0x10000)
				printf(" %-10.10s default GP group is "
				    "scgcef-contained\n", "");
			break;
		case ODK_PAGESIZE:
			printf(" %-10.10s\n", "PAGESIZE");
			break;
		default:
			break;
		}
		p += size - 8;
	}
}

static void
dump_mips_option_flags(const char *name, struct mips_option *opt, uint64_t info)
{
	int first;

	first = 1;
	for (; opt->desc != NULL; opt++) {
		if (info & opt->flag) {
			printf(" %-10.10s %s\n", first ? name : "",
			    opt->desc);
			first = 0;
		}
	}
}

static void
dump_mips_odk_reginfo(struct readcgcef *re, uint8_t *p, size_t sz)
{
	uint32_t ri_gprmask;
	uint32_t ri_cprmask[4];
	uint64_t ri_gp_value;
	uint8_t *pe;
	int i;

	pe = p + sz;
	while (p < pe) {
		ri_gprmask = re->dw_decode(&p, 4);
		/* Skip ri_pad padding field for mips64. */
		if (re->ec == CGCEFCLASS64)
			re->dw_decode(&p, 4);
		for (i = 0; i < 4; i++)
			ri_cprmask[i] = re->dw_decode(&p, 4);
		if (re->ec == CGCEFCLASS32)
			ri_gp_value = re->dw_decode(&p, 4);
		else
			ri_gp_value = re->dw_decode(&p, 8);
		printf(" %s    ", option_kind(ODK_REGINFO));
		printf("ri_gprmask:    0x%08jx\n", (uintmax_t) ri_gprmask);
		for (i = 0; i < 4; i++)
			printf("%11.11s ri_cprmask[%d]: 0x%08jx\n", "", i,
			    (uintmax_t) ri_cprmask[i]);
		printf("%12.12s", "");
		printf("ri_gp_value:   %#jx\n", (uintmax_t) ri_gp_value);
	}
}
#endif /* 0 */

static void
dump_arch_specific_info(struct readcgcef *re)
{

	//dump_liblist(re);
	//dump_attributes(re);

	switch (re->ehdr.e_machine) {
#if 0
	case EM_MIPS:
	case EM_MIPS_RS3_LE:
		dump_mips_specific_info(re);
#endif
	default:
		break;
	}
}

static void
dump_dwarf_line(struct readcgcef *re)
{
	struct section *s;
	Dwarf_Die die;
	Dwarf_Error de;
	Dwarf_Half tag, version, pointer_size;
	Dwarf_Unsigned offset, endoff, length, hdrlen, dirndx, mtime, fsize;
	Dwarf_Small minlen, defstmt, lrange, opbase, oplen;
	CGCEf_Data *d;
	char *pn;
	uint64_t address, file, line, column, isa, opsize, udelta;
	int64_t sdelta;
	uint8_t *p, *pe;
	int8_t lbase;
	int is_stmt, basic_block, end_sequence;
	int prologue_end, epilogue_begin;
	int i, dwarf_size, cgceferr, ret;

	printf("\nDump of debug contents of section .debug_line:\n");

	s = NULL;
	for (i = 0; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->name != NULL && !strcmp(s->name, ".debug_line"))
			break;
	}
	if ((size_t) i >= re->shnum)
		return;

	(void) cgcef_errno();
	if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_getdata failed: %s", cgcef_errmsg(-1));
		return;
	}
	if (d->d_size <= 0)
		return;

	while ((ret = dwarf_next_cu_header(re->dbg, NULL, NULL, NULL, NULL,
	    NULL, &de)) ==  DW_DLV_OK) {
		die = NULL;
		while (dwarf_siblingof(re->dbg, die, &die, &de) == DW_DLV_OK) {
			if (dwarf_tag(die, &tag, &de) != DW_DLV_OK) {
				warnx("dwarf_tag failed: %s",
				    dwarf_errmsg(de));
				return;
			}
			/* XXX: What about DW_TAG_partial_unit? */
			if (tag == DW_TAG_compile_unit)
				break;
		}
		if (die == NULL) {
			warnx("could not find DW_TAG_compile_unit die");
			return;
		}
		if (dwarf_attrval_unsigned(die, DW_AT_stmt_list, &offset,
		    &de) != DW_DLV_OK)
			continue;

		length = re->dw_read(d, &offset, 4);
		if (length == 0xffffffff) {
			dwarf_size = 8;
			length = re->dw_read(d, &offset, 8);
		} else
			dwarf_size = 4;

		if (length > d->d_size - offset) {
			warnx("invalid .dwarf_line section");
			continue;
		}

		endoff = offset + length;
		version = re->dw_read(d, &offset, 2);
		hdrlen = re->dw_read(d, &offset, dwarf_size);
		minlen = re->dw_read(d, &offset, 1);
		defstmt = re->dw_read(d, &offset, 1);
		lbase = re->dw_read(d, &offset, 1);
		lrange = re->dw_read(d, &offset, 1);
		opbase = re->dw_read(d, &offset, 1);

		printf("\n");
		printf("  Length:\t\t\t%ju\n", (uintmax_t) length);
		printf("  DWARF version:\t\t%u\n", version);
		printf("  Prologue Length:\t\t%ju\n", (uintmax_t) hdrlen);
		printf("  Minimum Instruction Length:\t%u\n", minlen);
		printf("  Initial value of 'is_stmt':\t%u\n", defstmt);
		printf("  Line Base:\t\t\t%d\n", lbase);
		printf("  Line Range:\t\t\t%u\n", lrange);
		printf("  Opcode Base:\t\t\t%u\n", opbase);
		(void) dwarf_get_address_size(re->dbg, &pointer_size, &de);
		printf("  (Pointer size:\t\t%u)\n", pointer_size);

		printf("\n");
		printf(" Opcodes:\n");
		for (i = 1; i < opbase; i++) {
			oplen = re->dw_read(d, &offset, 1);
			printf("  Opcode %d has %u args\n", i, oplen);
		}

		printf("\n");
		printf(" The Directory Table:\n");
		p = (uint8_t *) d->d_buf + offset;
		while (*p != '\0') {
			printf("  %s\n", (char *) p);
			p += strlen((char *) p) + 1;
		}

		p++;
		printf("\n");
		printf(" The File Name Table:\n");
		printf("  Entry\tDir\tTime\tSize\tName\n");
		i = 0;
		while (*p != '\0') {
			i++;
			pn = (char *) p;
			p += strlen(pn) + 1;
			dirndx = _decode_uleb128(&p);
			mtime = _decode_uleb128(&p);
			fsize = _decode_uleb128(&p);
			printf("  %d\t%ju\t%ju\t%ju\t%s\n", i,
			    (uintmax_t) dirndx, (uintmax_t) mtime,
			    (uintmax_t) fsize, pn);
		}

#define	RESET_REGISTERS						\
	do {							\
		address	       = 0;				\
		file	       = 1;				\
		line	       = 1;				\
		column	       = 0;				\
		is_stmt	       = defstmt;			\
		basic_block    = 0;				\
		end_sequence   = 0;				\
		prologue_end   = 0;				\
		epilogue_begin = 0;				\
	} while(0)

#define	LINE(x) (lbase + (((x) - opbase) % lrange))
#define	ADDRESS(x) ((((x) - opbase) / lrange) * minlen)

		p++;
		pe = (uint8_t *) d->d_buf + endoff;
		printf("\n");
		printf(" Line Number Statements:\n");

		RESET_REGISTERS;

		while (p < pe) {

			if (*p == 0) {
				/*
				 * Extended Opcodes.
				 */
				p++;
				opsize = _decode_uleb128(&p);
				printf("  Extended opcode %u: ", *p);
				switch (*p) {
				case DW_LNE_end_sequence:
					p++;
					end_sequence = 1;
					RESET_REGISTERS;
					printf("End of Sequence\n");
					break;
				case DW_LNE_set_address:
					p++;
					address = re->dw_decode(&p,
					    pointer_size);
					printf("set Address to %#jx\n",
					    (uintmax_t) address);
					break;
				case DW_LNE_define_file:
					p++;
					pn = (char *) p;
					p += strlen(pn) + 1;
					dirndx = _decode_uleb128(&p);
					mtime = _decode_uleb128(&p);
					fsize = _decode_uleb128(&p);
					printf("define new file: %s\n", pn);
					break;
				default:
					/* Unrecognized extened opcodes. */
					p += opsize;
					printf("unknown opcode\n");
				}
			} else if (*p > 0 && *p < opbase) {
				/*
				 * Standard Opcodes.
				 */
				switch(*p++) {
				case DW_LNS_copy:
					basic_block = 0;
					prologue_end = 0;
					epilogue_begin = 0;
					printf("  Copy\n");
					break;
				case DW_LNS_advance_pc:
					udelta = _decode_uleb128(&p) *
					    minlen;
					address += udelta;
					printf("  Advance PC by %ju to %#jx\n",
					    (uintmax_t) udelta,
					    (uintmax_t) address);
					break;
				case DW_LNS_advance_line:
					sdelta = _decode_sleb128(&p);
					line += sdelta;
					printf("  Advance Line by %jd to %ju\n",
					    (intmax_t) sdelta,
					    (uintmax_t) line);
					break;
				case DW_LNS_set_file:
					file = _decode_uleb128(&p);
					printf("  Set File to %ju\n",
					    (uintmax_t) file);
					break;
				case DW_LNS_set_column:
					column = _decode_uleb128(&p);
					printf("  Set Column to %ju\n",
					    (uintmax_t) column);
					break;
				case DW_LNS_negate_stmt:
					is_stmt = !is_stmt;
					printf("  Set is_stmt to %d\n", is_stmt);
					break;
				case DW_LNS_set_basic_block:
					basic_block = 1;
					printf("  Set basic block flag\n");
					break;
				case DW_LNS_const_add_pc:
					address += ADDRESS(255);
					printf("  Advance PC by constant %ju"
					    " to %#jx\n",
					    (uintmax_t) ADDRESS(255),
					    (uintmax_t) address);
					break;
				case DW_LNS_fixed_advance_pc:
					udelta = re->dw_decode(&p, 2);
					address += udelta;
					printf("  Advance PC by fixed value "
					    "%ju to %#jx\n",
					    (uintmax_t) udelta,
					    (uintmax_t) address);
					break;
				case DW_LNS_set_prologue_end:
					prologue_end = 1;
					printf("  Set prologue end flag\n");
					break;
				case DW_LNS_set_epilogue_begin:
					epilogue_begin = 1;
					printf("  Set epilogue begin flag\n");
					break;
				case DW_LNS_set_isa:
					isa = _decode_uleb128(&p);
					printf("  Set isa to %ju\n", isa);
					break;
				default:
					/* Unrecognized extended opcodes. */
					printf("  Unknown extended opcode %u\n",
					    *(p - 1));
					break;
				}

			} else {
				/*
				 * Special Opcodes.
				 */
				line += LINE(*p);
				address += ADDRESS(*p);
				basic_block = 0;
				prologue_end = 0;
				epilogue_begin = 0;
				printf("  Special opcode %u: advance Address "
				    "by %ju to %#jx and Line by %jd to %ju\n",
				    *p - opbase, (uintmax_t) ADDRESS(*p),
				    (uintmax_t) address, (intmax_t) LINE(*p),
				    (uintmax_t) line);
				p++;
			}


		}
	}
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_next_cu_header: %s", dwarf_errmsg(de));

#undef	RESET_REGISTERS
#undef	LINE
#undef	ADDRESS
}

static void
dump_dwarf_line_decoded(struct readcgcef *re)
{
	Dwarf_Die die;
	Dwarf_Line *linebuf, ln;
	Dwarf_Addr lineaddr;
	Dwarf_Signed linecount, srccount;
	Dwarf_Unsigned lineno, fn;
	Dwarf_Error de;
	const char *dir, *file;
	char **srcfiles;
	int i, ret;

	printf("Decoded dump of debug contents of section .debug_line:\n\n");
	while ((ret = dwarf_next_cu_header(re->dbg, NULL, NULL, NULL, NULL,
	    NULL, &de)) == DW_DLV_OK) {
		if (dwarf_siblingof(re->dbg, NULL, &die, &de) != DW_DLV_OK)
			continue;
		if (dwarf_attrval_string(die, DW_AT_name, &file, &de) !=
		    DW_DLV_OK)
			file = NULL;
		if (dwarf_attrval_string(die, DW_AT_comp_dir, &dir, &de) !=
		    DW_DLV_OK)
			dir = NULL;
		printf("CU: ");
		if (dir && file)
			printf("%s/", dir);
		if (file)
			printf("%s", file);
		putchar('\n');
		printf("%-37s %11s   %s\n", "Filename", "Line Number",
		    "Starting Address");
		if (dwarf_srclines(die, &linebuf, &linecount, &de) != DW_DLV_OK)
			continue;
		if (dwarf_srcfiles(die, &srcfiles, &srccount, &de) != DW_DLV_OK)
			continue;
		for (i = 0; i < linecount; i++) {
			ln = linebuf[i];
			if (dwarf_line_srcfileno(ln, &fn, &de) != DW_DLV_OK)
				continue;
			if (dwarf_lineno(ln, &lineno, &de) != DW_DLV_OK)
				continue;
			if (dwarf_lineaddr(ln, &lineaddr, &de) != DW_DLV_OK)
				continue;
			printf("%-37s %11ju %#18jx\n",
			    basename(srcfiles[fn - 1]), (uintmax_t) lineno,
			    (uintmax_t) lineaddr);
		}
		putchar('\n');
	}
}

static void
dump_dwarf_die(struct readcgcef *re, Dwarf_Die die, int level)
{
	Dwarf_Attribute *attr_list;
	Dwarf_Die ret_die;
	Dwarf_Off dieoff, cuoff, culen;
	Dwarf_Unsigned ate, v_udata;
	Dwarf_Signed attr_count, v_sdata;
	Dwarf_Off v_off;
	Dwarf_Addr v_addr;
	Dwarf_Half tag, attr, form;
	Dwarf_Block *v_block;
	Dwarf_Bool v_bool;
	Dwarf_Error de;
	const char *tag_str, *attr_str, *ate_str;
	char *v_str;
	uint8_t *b;
	int i, j, abc, ret;

	if (dwarf_dieoffset(die, &dieoff, &de) != DW_DLV_OK) {
		warnx("dwarf_dieoffset failed: %s", dwarf_errmsg(de));
		goto cont_search;
	}

	printf("<%d><%jx>: ", level, (uintmax_t) dieoff);

	if (dwarf_die_CU_offset_range(die, &cuoff, &culen, &de) != DW_DLV_OK) {
		warnx("dwarf_die_CU_offset_range failed: %s",
		      dwarf_errmsg(de));
		cuoff = 0;
	}

	abc = dwarf_die_abbrev_code(die);
	if (dwarf_tag(die, &tag, &de) != DW_DLV_OK) {
		warnx("dwarf_tag failed: %s", dwarf_errmsg(de));
		goto cont_search;
	}
	if (dwarf_get_TAG_name(tag, &tag_str) != DW_DLV_OK) {
		warnx("dwarf_get_TAG_name failed");
		goto cont_search;
	}

	printf("Abbrev Number: %d (%s)\n", abc, tag_str);

	if ((ret = dwarf_attrlist(die, &attr_list, &attr_count, &de)) !=
	    DW_DLV_OK) {
		if (ret == DW_DLV_ERROR)
			warnx("dwarf_attrlist failed: %s", dwarf_errmsg(de));
		goto cont_search;
	}

	for (i = 0; i < attr_count; i++) {
		if (dwarf_whatform(attr_list[i], &form, &de) != DW_DLV_OK) {
			warnx("dwarf_whatform failed: %s", dwarf_errmsg(de));
			continue;
		}
		if (dwarf_whatattr(attr_list[i], &attr, &de) != DW_DLV_OK) {
			warnx("dwarf_whatattr failed: %s", dwarf_errmsg(de));
			continue;
		}
		if (dwarf_get_AT_name(attr, &attr_str) != DW_DLV_OK) {
			warnx("dwarf_get_AT_name failed");
			continue;
		}
		printf("     %-18s: ", attr_str);
		switch (form) {
		case DW_FORM_ref_addr:
			if (dwarf_global_formref(attr_list[i], &v_off, &de) !=
			    DW_DLV_OK) {
				warnx("dwarf_global_formref failed: %s",
				    dwarf_errmsg(de));
				continue;
			}
			printf("<%jx>", (uintmax_t) v_off);
			break;

		case DW_FORM_ref1:
		case DW_FORM_ref2:
		case DW_FORM_ref4:
		case DW_FORM_ref8:
		case DW_FORM_ref_udata:
			if (dwarf_formref(attr_list[i], &v_off, &de) !=
			    DW_DLV_OK) {
				warnx("dwarf_formref failed: %s",
				    dwarf_errmsg(de));
				continue;
			}
			v_off += cuoff;
			printf("<%jx>", (uintmax_t) v_off);
			break;

		case DW_FORM_addr:
			if (dwarf_formaddr(attr_list[i], &v_addr, &de) !=
			    DW_DLV_OK) {
				warnx("dwarf_formaddr failed: %s",
				    dwarf_errmsg(de));
				continue;
			}
			printf("%#jx", (uintmax_t) v_addr);
			break;

		case DW_FORM_data1:
		case DW_FORM_data2:
		case DW_FORM_data4:
		case DW_FORM_data8:
		case DW_FORM_udata:
			if (dwarf_formudata(attr_list[i], &v_udata, &de) !=
			    DW_DLV_OK) {
				warnx("dwarf_formudata failed: %s",
				    dwarf_errmsg(de));
				continue;
			}
			printf("%ju", (uintmax_t) v_udata);
			break;

		case DW_FORM_sdata:
			if (dwarf_formsdata(attr_list[i], &v_sdata, &de) !=
			    DW_DLV_OK) {
				warnx("dwarf_formudata failed: %s",
				    dwarf_errmsg(de));
				continue;
			}
			printf("%jd", (intmax_t) v_sdata);
			break;

		case DW_FORM_flag:
			if (dwarf_formflag(attr_list[i], &v_bool, &de) !=
			    DW_DLV_OK) {
				warnx("dwarf_formflag failed: %s",
				    dwarf_errmsg(de));
				continue;
			}
			printf("%jd", (intmax_t) v_bool);
			break;

		case DW_FORM_string:
		case DW_FORM_strp:
			if (dwarf_formstring(attr_list[i], &v_str, &de) !=
			    DW_DLV_OK) {
				warnx("dwarf_formstring failed: %s",
				    dwarf_errmsg(de));
				continue;
			}
			if (form == DW_FORM_string)
				printf("%s", v_str);
			else
				printf("(indirect string) %s", v_str);
			break;

		case DW_FORM_block:
		case DW_FORM_block1:
		case DW_FORM_block2:
		case DW_FORM_block4:
			if (dwarf_formblock(attr_list[i], &v_block, &de) !=
			    DW_DLV_OK) {
				warnx("dwarf_formblock failed: %s",
				    dwarf_errmsg(de));
				continue;
			}
			printf("%ju byte block:", v_block->bl_len);
			b = v_block->bl_data;
			for (j = 0; (Dwarf_Unsigned) j < v_block->bl_len; j++)
				printf(" %x", b[j]);
			break;
		}
		switch (attr) {
		case DW_AT_encoding:
			if (dwarf_attrval_unsigned(die, attr, &ate, &de) !=
			    DW_DLV_OK)
				break;
			if (dwarf_get_ATE_name(ate, &ate_str) != DW_DLV_OK)
				break;
			printf("\t(%s)", &ate_str[strlen("DW_ATE_")]);
			break;
		default:
			break;
		}
		putchar('\n');
	}


cont_search:
	/* Search children. */
	ret = dwarf_child(die, &ret_die, &de);
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_child: %s", dwarf_errmsg(de));
	else if (ret == DW_DLV_OK)
		dump_dwarf_die(re, ret_die, level + 1);

	/* Search sibling. */
	ret = dwarf_siblingof(re->dbg, die, &ret_die, &de);
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_siblingof: %s", dwarf_errmsg(de));
	else if (ret == DW_DLV_OK)
		dump_dwarf_die(re, ret_die, level);

	dwarf_dealloc(re->dbg, die, DW_DLA_DIE);
}

static void
dump_dwarf_info(struct readcgcef *re)
{
	struct section *s;
	Dwarf_Die die;
	Dwarf_Error de;
	Dwarf_Half tag, version, pointer_size;
	Dwarf_Off cu_offset, cu_length;
	Dwarf_Off aboff;
	CGCEf_Data *d;
	int i, cgceferr, ret;

	printf("\nDump of debug contents of section .debug_info:\n");

	s = NULL;
	for (i = 0; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->name != NULL && !strcmp(s->name, ".debug_info"))
			break;
	}
	if ((size_t) i >= re->shnum)
		return;

	(void) cgcef_errno();
	if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_getdata failed: %s", cgcef_errmsg(-1));
		return;
	}
	if (d->d_size <= 0)
		return;

	while ((ret = dwarf_next_cu_header(re->dbg, NULL, &version, &aboff,
	    &pointer_size, NULL, &de)) == DW_DLV_OK) {
		die = NULL;
		while (dwarf_siblingof(re->dbg, die, &die, &de) == DW_DLV_OK) {
			if (dwarf_tag(die, &tag, &de) != DW_DLV_OK) {
				warnx("dwarf_tag failed: %s",
				    dwarf_errmsg(de));
				return;
			}
			/* XXX: What about DW_TAG_partial_unit? */
			if (tag == DW_TAG_compile_unit)
				break;
		}
		if (die == NULL) {
			warnx("could not find DW_TAG_compile_unit die");
			return;
		}

		if (dwarf_die_CU_offset_range(die, &cu_offset, &cu_length,
		    &de) != DW_DLV_OK) {
			warnx("dwarf_die_CU_offset failed: %s",
			    dwarf_errmsg(de));
			continue;
		}

		printf("  Compilation Unit @ %jd:\n", (intmax_t) cu_offset);
		printf("    Length:\t\t%jd\n", (intmax_t) cu_length);
		printf("    Version:\t\t%u\n", version);
		printf("    Abbrev Offset:\t%ju\n", (uintmax_t) aboff);
		printf("    Pointer Size:\t%u\n", pointer_size);

		dump_dwarf_die(re, die, 0);
	}
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_next_cu_header: %s", dwarf_errmsg(de));
}

static void
dump_dwarf_abbrev(struct readcgcef *re)
{
	Dwarf_Abbrev ab;
	Dwarf_Off aboff, atoff;
	Dwarf_Unsigned length, attr_count;
	Dwarf_Signed flag, form;
	Dwarf_Half tag, attr;
	Dwarf_Error de;
	const char *tag_str, *attr_str, *form_str;
	int i, j, ret;

	printf("\nContents of section .debug_abbrev:\n\n");

	while ((ret = dwarf_next_cu_header(re->dbg, NULL, NULL, &aboff,
	    NULL, NULL, &de)) ==  DW_DLV_OK) {
		printf("  Number TAG\n");
		i = 0;
		while ((ret = dwarf_get_abbrev(re->dbg, aboff, &ab, &length,
		    &attr_count, &de)) == DW_DLV_OK) {
			if (length == 1) {
				dwarf_dealloc(re->dbg, ab, DW_DLA_ABBREV);
				break;
			}
			aboff += length;
			printf("%4d", ++i);
			if (dwarf_get_abbrev_tag(ab, &tag, &de) != DW_DLV_OK) {
				warnx("dwarf_get_abbrev_tag failed: %s",
				    dwarf_errmsg(de));
				goto next_abbrev;
			}
			if (dwarf_get_TAG_name(tag, &tag_str) != DW_DLV_OK) {
				warnx("dwarf_get_TAG_name failed");
				goto next_abbrev;
			}
			if (dwarf_get_abbrev_children_flag(ab, &flag, &de) !=
			    DW_DLV_OK) {
				warnx("dwarf_get_abbrev_children_flag failed:"
				    " %s", dwarf_errmsg(de));
				goto next_abbrev;
			}
			printf("      %s    %s\n", tag_str,
			    flag ? "[has children]" : "[no children]");
			for (j = 0; (Dwarf_Unsigned) j < attr_count; j++) {
				if (dwarf_get_abbrev_entry(ab, (Dwarf_Signed) j,
				    &attr, &form, &atoff, &de) != DW_DLV_OK) {
					warnx("dwarf_get_abbrev_entry failed:"
					    " %s", dwarf_errmsg(de));
					continue;
				}
				if (dwarf_get_AT_name(attr, &attr_str) !=
				    DW_DLV_OK) {
					warnx("dwarf_get_AT_name failed");
					continue;
				}
				if (dwarf_get_FORM_name(form, &form_str) !=
				    DW_DLV_OK) {
					warnx("dwarf_get_FORM_name failed");
					continue;
				}
				printf("    %-18s %s\n", attr_str, form_str);
			}
		next_abbrev:
			dwarf_dealloc(re->dbg, ab, DW_DLA_ABBREV);
		}
		if (ret != DW_DLV_OK)
			warnx("dwarf_get_abbrev: %s", dwarf_errmsg(de));
	}
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_next_cu_header: %s", dwarf_errmsg(de));
}

static void
dump_dwarf_pubnames(struct readcgcef *re)
{
	struct section *s;
	Dwarf_Off die_off;
	Dwarf_Unsigned offset, length, nt_cu_offset, nt_cu_length;
	Dwarf_Signed cnt;
	Dwarf_Global *globs;
	Dwarf_Half nt_version;
	Dwarf_Error de;
	CGCEf_Data *d;
	char *glob_name;
	int i, dwarf_size, cgceferr;

	printf("\nContents of the .debug_pubnames section:\n");

	s = NULL;
	for (i = 0; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->name != NULL && !strcmp(s->name, ".debug_pubnames"))
			break;
	}
	if ((size_t) i >= re->shnum)
		return;

	(void) cgcef_errno();
	if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_getdata failed: %s", cgcef_errmsg(-1));
		return;
	}
	if (d->d_size <= 0)
		return;

	/* Read in .debug_pubnames section table header. */
	offset = 0;
	length = re->dw_read(d, &offset, 4);
	if (length == 0xffffffff) {
		dwarf_size = 8;
		length = re->dw_read(d, &offset, 8);
	} else
		dwarf_size = 4;

	if (length > d->d_size - offset) {
		warnx("invalid .dwarf_pubnames section");
		return;
	}

	nt_version = re->dw_read(d, &offset, 2);
	nt_cu_offset = re->dw_read(d, &offset, dwarf_size);
	nt_cu_length = re->dw_read(d, &offset, dwarf_size);
	printf("  Length:\t\t\t\t%ju\n", (uintmax_t) length);
	printf("  Version:\t\t\t\t%u\n", nt_version);
	printf("  Offset into .debug_info section:\t%ju\n",
	    (uintmax_t) nt_cu_offset);
	printf("  Size of area in .debug_info section:\t%ju\n",
	    (uintmax_t) nt_cu_length);

	if (dwarf_get_globals(re->dbg, &globs, &cnt, &de) != DW_DLV_OK) {
		warnx("dwarf_get_globals failed: %s", dwarf_errmsg(de));
		return;
	}

	printf("\n    Offset      Name\n");
	for (i = 0; i < cnt; i++) {
		if (dwarf_globname(globs[i], &glob_name, &de) != DW_DLV_OK) {
			warnx("dwarf_globname failed: %s", dwarf_errmsg(de));
			continue;
		}
		if (dwarf_global_die_offset(globs[i], &die_off, &de) !=
		    DW_DLV_OK) {
			warnx("dwarf_global_die_offset failed: %s",
			    dwarf_errmsg(de));
			continue;
		}
		printf("    %-11ju %s\n", (uintmax_t) die_off, glob_name);
	}
}

static void
dump_dwarf_aranges(struct readcgcef *re)
{
	struct section *s;
	Dwarf_Arange *aranges;
	Dwarf_Addr start;
	Dwarf_Unsigned offset, length, as_cu_offset;
	Dwarf_Off die_off;
	Dwarf_Signed cnt;
	Dwarf_Half as_version, as_addrsz, as_segsz;
	Dwarf_Error de;
	CGCEf_Data *d;
	int i, dwarf_size, cgceferr;

	printf("\nContents of section .debug_aranges:\n");

	s = NULL;
	for (i = 0; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->name != NULL && !strcmp(s->name, ".debug_aranges"))
			break;
	}
	if ((size_t) i >= re->shnum)
		return;

	(void) cgcef_errno();
	if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_getdata failed: %s", cgcef_errmsg(-1));
		return;
	}
	if (d->d_size <= 0)
		return;

	/* Read in the .debug_aranges section table header. */
	offset = 0;
	length = re->dw_read(d, &offset, 4);
	if (length == 0xffffffff) {
		dwarf_size = 8;
		length = re->dw_read(d, &offset, 8);
	} else
		dwarf_size = 4;

	if (length > d->d_size - offset) {
		warnx("invalid .dwarf_aranges section");
		return;
	}

	as_version = re->dw_read(d, &offset, 2);
	as_cu_offset = re->dw_read(d, &offset, dwarf_size);
	as_addrsz = re->dw_read(d, &offset, 1);
	as_segsz = re->dw_read(d, &offset, 1);

	printf("  Length:\t\t\t%ju\n", (uintmax_t) length);
	printf("  Version:\t\t\t%u\n", as_version);
	printf("  Offset into .debug_info:\t%ju\n", (uintmax_t) as_cu_offset);
	printf("  Pointer Size:\t\t\t%u\n", as_addrsz);
	printf("  Segment Size:\t\t\t%u\n", as_segsz);

	if (dwarf_get_aranges(re->dbg, &aranges, &cnt, &de) != DW_DLV_OK) {
		warnx("dwarf_get_aranges failed: %s", dwarf_errmsg(de));
		return;
	}

	printf("\n    Address  Length\n");
	for (i = 0; i < cnt; i++) {
		if (dwarf_get_arange_info(aranges[i], &start, &length,
		    &die_off, &de) != DW_DLV_OK) {
			warnx("dwarf_get_arange_info failed: %s",
			    dwarf_errmsg(de));
			continue;
		}
		printf("    %08jx %ju\n", (uintmax_t) start,
		    (uintmax_t) length);
	}
}

static void
dump_dwarf_ranges_foreach(struct readcgcef *re, Dwarf_Die die, Dwarf_Addr base)
{
	Dwarf_Attribute *attr_list;
	Dwarf_Ranges *ranges;
	Dwarf_Die ret_die;
	Dwarf_Error de;
	Dwarf_Addr base0;
	Dwarf_Half attr;
	Dwarf_Signed attr_count, cnt;
	Dwarf_Unsigned off, bytecnt;
	int i, j, ret;

	if ((ret = dwarf_attrlist(die, &attr_list, &attr_count, &de)) !=
	    DW_DLV_OK) {
		if (ret == DW_DLV_ERROR)
			warnx("dwarf_attrlist failed: %s", dwarf_errmsg(de));
		goto cont_search;
	}

	for (i = 0; i < attr_count; i++) {
		if (dwarf_whatattr(attr_list[i], &attr, &de) != DW_DLV_OK) {
			warnx("dwarf_whatattr failed: %s", dwarf_errmsg(de));
			continue;
		}
		if (attr != DW_AT_ranges)
			continue;
		if (dwarf_formudata(attr_list[i], &off, &de) != DW_DLV_OK) {
			warnx("dwarf_formudata failed: %s", dwarf_errmsg(de));
			continue;
		}
		if (dwarf_get_ranges(re->dbg, (Dwarf_Off) off, &ranges, &cnt,
		    &bytecnt, &de) != DW_DLV_OK)
			continue;
		base0 = base;
		for (j = 0; j < cnt; j++) {
			printf("    %08jx ", (uintmax_t) off);
			if (ranges[j].dwr_type == DW_RANGES_END) {
				printf("%s\n", "<End of list>");
				continue;
			} else if (ranges[j].dwr_type ==
			    DW_RANGES_ADDRESS_SELECTION) {
				base0 = ranges[j].dwr_addr2;
				continue;
			}
			if (re->ec == CGCEFCLASS32)
				printf("%08jx %08jx\n",
				    ranges[j].dwr_addr1 + base0,
				    ranges[j].dwr_addr2 + base0);
			else
				printf("%016jx %016jx\n",
				    ranges[j].dwr_addr1 + base0,
				    ranges[j].dwr_addr2 + base0);
		}
	}

cont_search:
	/* Search children. */
	ret = dwarf_child(die, &ret_die, &de);
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_child: %s", dwarf_errmsg(de));
	else if (ret == DW_DLV_OK)
		dump_dwarf_ranges_foreach(re, ret_die, base);

	/* Search sibling. */
	ret = dwarf_siblingof(re->dbg, die, &ret_die, &de);
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_siblingof: %s", dwarf_errmsg(de));
	else if (ret == DW_DLV_OK)
		dump_dwarf_ranges_foreach(re, ret_die, base);
}

static void
dump_dwarf_ranges(struct readcgcef *re)
{
	Dwarf_Ranges *ranges;
	Dwarf_Die die;
	Dwarf_Signed cnt;
	Dwarf_Unsigned bytecnt;
	Dwarf_Half tag;
	Dwarf_Error de;
	Dwarf_Unsigned lowpc;
	int ret;

	if (dwarf_get_ranges(re->dbg, 0, &ranges, &cnt, &bytecnt, &de) !=
	    DW_DLV_OK)
		return;

	printf("Contents of the .debug_ranges section:\n\n");
	if (re->ec == CGCEFCLASS32)
		printf("    %-8s %-8s %s\n", "Offset", "Begin", "End");
	else
		printf("    %-8s %-16s %s\n", "Offset", "Begin", "End");

	while ((ret = dwarf_next_cu_header(re->dbg, NULL, NULL, NULL, NULL,
	    NULL, &de)) == DW_DLV_OK) {
		die = NULL;
		if (dwarf_siblingof(re->dbg, die, &die, &de) != DW_DLV_OK)
			continue;
		if (dwarf_tag(die, &tag, &de) != DW_DLV_OK) {
			warnx("dwarf_tag failed: %s", dwarf_errmsg(de));
			continue;
		}
		/* XXX: What about DW_TAG_partial_unit? */
		lowpc = 0;
		if (tag == DW_TAG_compile_unit) {
			if (dwarf_attrval_unsigned(die, DW_AT_low_pc, &lowpc,
			    &de) != DW_DLV_OK)
				lowpc = 0;
		}

		dump_dwarf_ranges_foreach(re, die, (Dwarf_Addr) lowpc);
	}
	putchar('\n');
}

static void
dump_dwarf_macinfo(struct readcgcef *re)
{
	Dwarf_Unsigned offset;
	Dwarf_Signed cnt;
	Dwarf_Macro_Details *md;
	Dwarf_Error de;
	const char *mi_str;
	int i;

#define	_MAX_MACINFO_ENTRY	65535

	printf("\nContents of section .debug_macinfo:\n\n");

	offset = 0;
	while (dwarf_get_macro_details(re->dbg, offset, _MAX_MACINFO_ENTRY,
	    &cnt, &md, &de) == DW_DLV_OK) {
		for (i = 0; i < cnt; i++) {
			offset = md[i].dmd_offset + 1;
			if (md[i].dmd_type == 0)
				break;
			if (dwarf_get_MACINFO_name(md[i].dmd_type, &mi_str) !=
			    DW_DLV_OK) {
				warnx("dwarf_get_MACINFO_name failed: %s",
				    dwarf_errmsg(de));
				continue;
			}
			printf(" %s", mi_str);
			switch (md[i].dmd_type) {
			case DW_MACINFO_define:
			case DW_MACINFO_undef:
				printf(" - lineno : %jd macro : %s\n",
				    (intmax_t) md[i].dmd_lineno,
				    md[i].dmd_macro);
				break;
			case DW_MACINFO_start_file:
				printf(" - lineno : %jd filenum : %jd\n",
				    (intmax_t) md[i].dmd_lineno,
				    (intmax_t) md[i].dmd_fileindex);
				break;
			default:
				putchar('\n');
				break;
			}
		}
	}

#undef	_MAX_MACINFO_ENTRY
}

static void
dump_dwarf_frame_inst(Dwarf_Cie cie, uint8_t *insts, Dwarf_Unsigned len,
    Dwarf_Unsigned caf, Dwarf_Signed daf, Dwarf_Addr pc, Dwarf_Debug dbg)
{
	Dwarf_Frame_Op *oplist;
	Dwarf_Signed opcnt, delta;
	Dwarf_Small op;
	Dwarf_Error de;
	const char *op_str;
	int i;

	if (dwarf_expand_frame_instructions(cie, insts, len, &oplist,
	    &opcnt, &de) != DW_DLV_OK) {
		warnx("dwarf_expand_frame_instructions failed: %s",
		    dwarf_errmsg(de));
		return;
	}

	for (i = 0; i < opcnt; i++) {
		if (oplist[i].fp_base_op != 0)
			op = oplist[i].fp_base_op << 6;
		else
			op = oplist[i].fp_extended_op;
		if (dwarf_get_CFA_name(op, &op_str) != DW_DLV_OK) {
			warnx("dwarf_get_CFA_name failed: %s",
			    dwarf_errmsg(de));
			continue;
		}
		printf("  %s", op_str);
		switch (op) {
		case DW_CFA_advance_loc:
			delta = oplist[i].fp_offset * caf;
			pc += delta;
			printf(": %ju to %08jx", (uintmax_t) delta,
			    (uintmax_t) pc);
			break;
		case DW_CFA_offset:
		case DW_CFA_offset_extended:
		case DW_CFA_offset_extended_sf:
			delta = oplist[i].fp_offset * daf;
			printf(": r%u at cfa%+jd", oplist[i].fp_register,
			    (intmax_t) delta);
			break;
		case DW_CFA_restore:
			printf(": r%u", oplist[i].fp_register);
			break;
		case DW_CFA_set_loc:
			pc = oplist[i].fp_offset;
			printf(": to %08jx", (uintmax_t) pc);
			break;
		case DW_CFA_advance_loc1:
		case DW_CFA_advance_loc2:
		case DW_CFA_advance_loc4:
			pc += oplist[i].fp_offset;
			printf(": %jd to %08jx", (intmax_t) oplist[i].fp_offset,
			    (uintmax_t) pc);
			break;
		case DW_CFA_def_cfa:
			printf(": r%u ofs %ju", oplist[i].fp_register,
			    (uintmax_t) oplist[i].fp_offset);
			break;
		case DW_CFA_def_cfa_sf:
			printf(": r%u ofs %jd", oplist[i].fp_register,
			    (intmax_t) (oplist[i].fp_offset * daf));
			break;
		case DW_CFA_def_cfa_register:
			printf(": r%u", oplist[i].fp_register);
			break;
		case DW_CFA_def_cfa_offset:
			printf(": %ju", (uintmax_t) oplist[i].fp_offset);
			break;
		case DW_CFA_def_cfa_offset_sf:
			printf(": %jd", (intmax_t) (oplist[i].fp_offset * daf));
			break;
		default:
			break;
		}
		putchar('\n');
	}

	dwarf_dealloc(dbg, oplist, DW_DLA_FRAME_BLOCK);
}

static char *
get_regoff_str(Dwarf_Half reg, Dwarf_Addr off)
{
	static char rs[16];

	if (reg == DW_FRAME_UNDEFINED_VAL || reg == DW_FRAME_REG_INITIAL_VALUE)
		snprintf(rs, sizeof(rs), "%c", 'u');
	else if (reg == DW_FRAME_CFA_COL)
		snprintf(rs, sizeof(rs), "c%+jd", (intmax_t) off);
	else
		snprintf(rs, sizeof(rs), "r%u%+jd", reg, (intmax_t) off);

	return (rs);
}

static int
dump_dwarf_frame_regtable(Dwarf_Fde fde, Dwarf_Addr pc, Dwarf_Unsigned func_len,
    Dwarf_Half cie_ra)
{
	Dwarf_Regtable rt;
	Dwarf_Addr row_pc, end_pc, pre_pc, cur_pc;
	Dwarf_Error de;
	char rn[16];
	char *vec;
	int i;

#define BIT_SET(v, n) (v[(n)>>3] |= 1U << ((n) & 7))
#define BIT_CLR(v, n) (v[(n)>>3] &= ~(1U << ((n) & 7)))
#define BIT_ISSET(v, n) (v[(n)>>3] & (1U << ((n) & 7)))
#define	RT(x) rt.rules[(x)]

	vec = calloc((DW_REG_TABLE_SIZE + 7) / 8, 1);
	if (vec == NULL)
		err(EXIT_FAILURE, "calloc failed");

	pre_pc = ~((Dwarf_Addr) 0);
	cur_pc = pc;
	end_pc = pc + func_len;
	for (; cur_pc < end_pc; cur_pc++) {
		if (dwarf_get_fde_info_for_all_regs(fde, cur_pc, &rt, &row_pc,
		    &de) != DW_DLV_OK) {
			warnx("dwarf_get_fde_info_for_all_regs failed: %s\n",
			    dwarf_errmsg(de));
			return (-1);
		}
		if (row_pc == pre_pc)
			continue;
		pre_pc = row_pc;
		for (i = 1; i < DW_REG_TABLE_SIZE; i++) {
			if (rt.rules[i].dw_regnum != DW_FRAME_REG_INITIAL_VALUE)
				BIT_SET(vec, i);
		}
	}

	printf("   LOC   CFA      ");
	for (i = 1; i < DW_REG_TABLE_SIZE; i++) {
		if (BIT_ISSET(vec, i)) {
			if ((Dwarf_Half) i == cie_ra)
				printf("ra   ");
			else {
				snprintf(rn, sizeof(rn), "r%d", i);
				printf("%-5s", rn);
			}
		}
	}
	putchar('\n');

	pre_pc = ~((Dwarf_Addr) 0);
	cur_pc = pc;
	end_pc = pc + func_len;
	for (; cur_pc < end_pc; cur_pc++) {
		if (dwarf_get_fde_info_for_all_regs(fde, cur_pc, &rt, &row_pc,
		    &de) != DW_DLV_OK) {
			warnx("dwarf_get_fde_info_for_all_regs failed: %s\n",
			    dwarf_errmsg(de));
			return (-1);
		}
		if (row_pc == pre_pc)
			continue;
		pre_pc = row_pc;
		printf("%08jx ", (uintmax_t) row_pc);
		printf("%-8s ", get_regoff_str(RT(0).dw_regnum,
		    RT(0).dw_offset));
		for (i = 1; i < DW_REG_TABLE_SIZE; i++) {
			if (BIT_ISSET(vec, i)) {
				printf("%-5s", get_regoff_str(RT(i).dw_regnum,
				    RT(i).dw_offset));
			}
		}
		putchar('\n');
	}

	free(vec);

	return (0);

#undef	BIT_SET
#undef	BIT_CLR
#undef	BIT_ISSET
#undef	RT
}

static void
dump_dwarf_frame_section(struct readcgcef *re, struct section *s, int alt)
{
	Dwarf_Cie *cie_list, cie, pre_cie;
	Dwarf_Fde *fde_list, fde;
	Dwarf_Off cie_offset, fde_offset;
	Dwarf_Unsigned cie_length, fde_instlen;
	Dwarf_Unsigned cie_caf, cie_daf, cie_instlen, func_len, fde_length;
	Dwarf_Signed cie_count, fde_count, cie_index;
	Dwarf_Addr low_pc;
	Dwarf_Half cie_ra;
	Dwarf_Small cie_version;
	Dwarf_Ptr fde_addr, fde_inst, cie_inst;
	char *cie_aug, c;
	int i, eh_frame;
	Dwarf_Error de;

	printf("\nThe section %s contains:\n\n", s->name);

	if (!strcmp(s->name, ".debug_frame")) {
		eh_frame = 0;
		if (dwarf_get_fde_list(re->dbg, &cie_list, &cie_count,
		    &fde_list, &fde_count, &de) != DW_DLV_OK) {
			warnx("dwarf_get_fde_list failed: %s",
			    dwarf_errmsg(de));
			return;
		}
	} else if (!strcmp(s->name, ".eh_frame")) {
		eh_frame = 1;
		if (dwarf_get_fde_list_eh(re->dbg, &cie_list, &cie_count,
		    &fde_list, &fde_count, &de) != DW_DLV_OK) {
			warnx("dwarf_get_fde_list_eh failed: %s",
			    dwarf_errmsg(de));
			return;
		}
	} else
		return;

	pre_cie = NULL;
	for (i = 0; i < fde_count; i++) {
		if (dwarf_get_fde_n(fde_list, i, &fde, &de) != DW_DLV_OK) {
			warnx("dwarf_get_fde_n failed: %s", dwarf_errmsg(de));
			continue;
		}
		if (dwarf_get_cie_of_fde(fde, &cie, &de) != DW_DLV_OK) {
			warnx("dwarf_get_fde_n failed: %s", dwarf_errmsg(de));
			continue;
		}
		if (dwarf_get_fde_range(fde, &low_pc, &func_len, &fde_addr,
		    &fde_length, &cie_offset, &cie_index, &fde_offset,
		    &de) != DW_DLV_OK) {
			warnx("dwarf_get_fde_range failed: %s",
			    dwarf_errmsg(de));
			continue;
		}
		if (dwarf_get_fde_instr_bytes(fde, &fde_inst, &fde_instlen,
		    &de) != DW_DLV_OK) {
			warnx("dwarf_get_fde_instr_bytes failed: %s",
			    dwarf_errmsg(de));
			continue;
		}
		if (pre_cie == NULL || cie != pre_cie) {
			pre_cie = cie;
			if (dwarf_get_cie_info(cie, &cie_length, &cie_version,
			    &cie_aug, &cie_caf, &cie_daf, &cie_ra,
			    &cie_inst, &cie_instlen, &de) != DW_DLV_OK) {
				warnx("dwarf_get_cie_info failed: %s",
				    dwarf_errmsg(de));
				continue;
			}
			printf("%08jx %08jx %8.8jx CIE",
			    (uintmax_t) cie_offset,
			    (uintmax_t) cie_length,
			    (uintmax_t) (eh_frame ? 0 : ~0U));
			if (!alt) {
				putchar('\n');
				printf("  Version:\t\t\t%u\n", cie_version);
				printf("  Augmentation:\t\t\t\"");
				while ((c = *cie_aug++) != '\0')
					putchar(c);
				printf("\"\n");
				printf("  Code alignment factor:\t%ju\n",
				    (uintmax_t) cie_caf);
				printf("  Data alignment factor:\t%jd\n",
				    (intmax_t) cie_daf);
				printf("  Return address column:\t%ju\n",
				    (uintmax_t) cie_ra);
				putchar('\n');
				dump_dwarf_frame_inst(cie, cie_inst,
				    cie_instlen, cie_caf, cie_daf, 0,
				    re->dbg);
				putchar('\n');
			} else {
				printf(" \"");
				while ((c = *cie_aug++) != '\0')
					putchar(c);
				putchar('"');
				printf(" cf=%ju df=%jd ra=%ju\n",
				    (uintmax_t) cie_caf,
				    (uintmax_t) cie_daf,
				    (uintmax_t) cie_ra);
				dump_dwarf_frame_regtable(fde, low_pc, 1,
				    cie_ra);
				putchar('\n');
			}
		}
		printf("%08jx %08jx %08jx FDE cie=%08jx pc=%08jx..%08jx\n",
		    (uintmax_t) fde_offset, (uintmax_t) fde_length,
		    (uintmax_t) cie_offset,
		    (uintmax_t) (eh_frame ? fde_offset + 4 - cie_offset :
			cie_offset),
		    (uintmax_t) low_pc, (uintmax_t) (low_pc + func_len));
		if (!alt)
			dump_dwarf_frame_inst(cie, fde_inst, fde_instlen,
			    cie_caf, cie_daf, low_pc, re->dbg);
		else
			dump_dwarf_frame_regtable(fde, low_pc, func_len,
			    cie_ra);
		putchar('\n');
	}
}

static void
dump_dwarf_frame(struct readcgcef *re, int alt)
{
	struct section *s;
	int i;

	(void) dwarf_set_frame_cfa_value(re->dbg, DW_FRAME_CFA_COL);

	for (i = 0; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->name != NULL && (!strcmp(s->name, ".debug_frame") ||
		    !strcmp(s->name, ".eh_frame")))
			dump_dwarf_frame_section(re, s, alt);
	}
}

static void
dump_dwarf_str(struct readcgcef *re)
{
	struct section *s;
	CGCEf_Data *d;
	unsigned char *p;
	int cgceferr, end, i, j;

	printf("\nContents of section .debug_str:\n");

	s = NULL;
	for (i = 0; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (s->name != NULL && !strcmp(s->name, ".debug_str"))
			break;
	}
	if ((size_t) i >= re->shnum)
		return;

	(void) cgcef_errno();
	if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_getdata failed: %s", cgcef_errmsg(-1));
		return;
	}
	if (d->d_size <= 0)
		return;

	for (i = 0, p = d->d_buf; (size_t) i < d->d_size; i += 16) {
		printf("  0x%08x", (unsigned int) i);
		if ((size_t) i + 16 > d->d_size)
			end = d->d_size;
		else
			end = i + 16;
		for (j = i; j < i + 16; j++) {
			if ((j - i) % 4 == 0)
				putchar(' ');
			if (j >= end) {
				printf("  ");
				continue;
			}
			printf("%02x", (uint8_t) p[j]);
		}
		putchar(' ');
		for (j = i; j < end; j++) {
			if (isprint(p[j]))
				putchar(p[j]);
			else if (p[j] == 0)
				putchar('.');
			else
				putchar(' ');
		}
		putchar('\n');
	}
}

struct loc_at {
	Dwarf_Attribute la_at;
	Dwarf_Unsigned la_off;
	Dwarf_Unsigned la_lowpc;
	TAILQ_ENTRY(loc_at) la_next;
};

static TAILQ_HEAD(, loc_at) lalist = TAILQ_HEAD_INITIALIZER(lalist);

static void
search_loclist_at(struct readcgcef *re, Dwarf_Die die, Dwarf_Unsigned lowpc)
{
	Dwarf_Attribute *attr_list;
	Dwarf_Die ret_die;
	Dwarf_Unsigned off;
	Dwarf_Signed attr_count;
	Dwarf_Half attr, form;
	Dwarf_Error de;
	struct loc_at *la, *nla;
	int i, ret;

	if ((ret = dwarf_attrlist(die, &attr_list, &attr_count, &de)) !=
	    DW_DLV_OK) {
		if (ret == DW_DLV_ERROR)
			warnx("dwarf_attrlist failed: %s", dwarf_errmsg(de));
		goto cont_search;
	}
	for (i = 0; i < attr_count; i++) {
		if (dwarf_whatattr(attr_list[i], &attr, &de) != DW_DLV_OK) {
			warnx("dwarf_whatattr failed: %s", dwarf_errmsg(de));
			continue;
		}
		if (attr != DW_AT_location &&
		    attr != DW_AT_string_length &&
		    attr != DW_AT_return_addr &&
		    attr != DW_AT_data_member_location &&
		    attr != DW_AT_frame_base &&
		    attr != DW_AT_segment &&
		    attr != DW_AT_static_link &&
		    attr != DW_AT_use_location &&
		    attr != DW_AT_vtable_elem_location)
			continue;
		if (dwarf_whatform(attr_list[i], &form, &de) != DW_DLV_OK) {
			warnx("dwarf_whatform failed: %s", dwarf_errmsg(de));
			continue;
		}
		if (form != DW_FORM_data4 && form != DW_FORM_data8)
			continue;
		if (dwarf_formudata(attr_list[i], &off, &de) != DW_DLV_OK) {
			warnx("dwarf_formudata failed: %s", dwarf_errmsg(de));
			continue;
		}
		TAILQ_FOREACH(la, &lalist, la_next) {
			if (off == la->la_off)
				break;
			if (off < la->la_off) {
				if ((nla = malloc(sizeof(*nla))) == NULL)
					err(EXIT_FAILURE, "malloc failed");
				nla->la_at = attr_list[i];
				nla->la_off = off;
				nla->la_lowpc = lowpc;
				TAILQ_INSERT_BEFORE(la, nla, la_next);
				break;
			}
		}
		if (la == NULL) {
			if ((nla = malloc(sizeof(*nla))) == NULL)
				err(EXIT_FAILURE, "malloc failed");
			nla->la_at = attr_list[i];
			nla->la_off = off;
			nla->la_lowpc = lowpc;
			TAILQ_INSERT_TAIL(&lalist, nla, la_next);
		}
	}

cont_search:
	/* Search children. */
	ret = dwarf_child(die, &ret_die, &de);
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_child: %s", dwarf_errmsg(de));
	else if (ret == DW_DLV_OK)
		search_loclist_at(re, ret_die, lowpc);

	/* Search sibling. */
	ret = dwarf_siblingof(re->dbg, die, &ret_die, &de);
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_siblingof: %s", dwarf_errmsg(de));
	else if (ret == DW_DLV_OK)
		search_loclist_at(re, ret_die, lowpc);
}

static void
dump_dwarf_loclist(struct readcgcef *re)
{
	Dwarf_Die die;
	Dwarf_Locdesc **llbuf;
	Dwarf_Unsigned lowpc;
	Dwarf_Signed lcnt;
	Dwarf_Half tag;
	Dwarf_Error de;
	Dwarf_Loc *lr;
	struct loc_at *la;
	const char *op_str;
	int i, j, ret;

	printf("\nContents of section .debug_loc:\n");

	while ((ret = dwarf_next_cu_header(re->dbg, NULL, NULL, NULL, NULL,
	    NULL, &de)) == DW_DLV_OK) {
		die = NULL;
		if (dwarf_siblingof(re->dbg, die, &die, &de) != DW_DLV_OK)
			continue;
		if (dwarf_tag(die, &tag, &de) != DW_DLV_OK) {
			warnx("dwarf_tag failed: %s", dwarf_errmsg(de));
			continue;
		}
		/* XXX: What about DW_TAG_partial_unit? */
		lowpc = 0;
		if (tag == DW_TAG_compile_unit) {
			if (dwarf_attrval_unsigned(die, DW_AT_low_pc, &lowpc,
			    &de) != DW_DLV_OK)
				lowpc = 0;
		}

		/* Search attributes for reference to .debug_loc section. */
		search_loclist_at(re, die, lowpc);
	}
	if (ret == DW_DLV_ERROR)
		warnx("dwarf_next_cu_header: %s", dwarf_errmsg(de));

	if (TAILQ_EMPTY(&lalist))
		return;

	printf("    Offset   Begin    End      Expression\n");

	TAILQ_FOREACH(la, &lalist, la_next) {
		if (dwarf_loclist_n(la->la_at, &llbuf, &lcnt, &de) !=
		    DW_DLV_OK) {
			warnx("dwarf_loclist_n failed: %s", dwarf_errmsg(de));
			continue;
		}
		for (i = 0; i < lcnt; i++) {
			printf("    %8.8jx ", la->la_off);
			if (llbuf[i]->ld_lopc == 0 && llbuf[i]->ld_hipc == 0) {
				printf("<End of list>\n");
				continue;
			}

			/* TODO: handle base selection entry. */

			printf("%8.8jx %8.8jx ",
			    (uintmax_t) (la->la_lowpc + llbuf[i]->ld_lopc),
			    (uintmax_t) (la->la_lowpc + llbuf[i]->ld_hipc));

			putchar('(');
			for (j = 0; (Dwarf_Half) j < llbuf[i]->ld_cents; j++) {
				lr = &llbuf[i]->ld_s[j];
				if (dwarf_get_OP_name(lr->lr_atom, &op_str) !=
				    DW_DLV_OK) {
					warnx("dwarf_get_OP_name failed: %s",
					    dwarf_errmsg(de));
					continue;
				}

				printf("%s", op_str);

				switch (lr->lr_atom) {
				/* Operations with no operands. */
				case DW_OP_deref:
				case DW_OP_reg0:
				case DW_OP_reg1:
				case DW_OP_reg2:
				case DW_OP_reg3:
				case DW_OP_reg4:
				case DW_OP_reg5:
				case DW_OP_reg6:
				case DW_OP_reg7:
				case DW_OP_reg8:
				case DW_OP_reg9:
				case DW_OP_reg10:
				case DW_OP_reg11:
				case DW_OP_reg12:
				case DW_OP_reg13:
				case DW_OP_reg14:
				case DW_OP_reg15:
				case DW_OP_reg16:
				case DW_OP_reg17:
				case DW_OP_reg18:
				case DW_OP_reg19:
				case DW_OP_reg20:
				case DW_OP_reg21:
				case DW_OP_reg22:
				case DW_OP_reg23:
				case DW_OP_reg24:
				case DW_OP_reg25:
				case DW_OP_reg26:
				case DW_OP_reg27:
				case DW_OP_reg28:
				case DW_OP_reg29:
				case DW_OP_reg30:
				case DW_OP_reg31:
				case DW_OP_lit0:
				case DW_OP_lit1:
				case DW_OP_lit2:
				case DW_OP_lit3:
				case DW_OP_lit4:
				case DW_OP_lit5:
				case DW_OP_lit6:
				case DW_OP_lit7:
				case DW_OP_lit8:
				case DW_OP_lit9:
				case DW_OP_lit10:
				case DW_OP_lit11:
				case DW_OP_lit12:
				case DW_OP_lit13:
				case DW_OP_lit14:
				case DW_OP_lit15:
				case DW_OP_lit16:
				case DW_OP_lit17:
				case DW_OP_lit18:
				case DW_OP_lit19:
				case DW_OP_lit20:
				case DW_OP_lit21:
				case DW_OP_lit22:
				case DW_OP_lit23:
				case DW_OP_lit24:
				case DW_OP_lit25:
				case DW_OP_lit26:
				case DW_OP_lit27:
				case DW_OP_lit28:
				case DW_OP_lit29:
				case DW_OP_lit30:
				case DW_OP_lit31:
				case DW_OP_dup:
				case DW_OP_drop:
				case DW_OP_over:
				case DW_OP_swap:
				case DW_OP_rot:
				case DW_OP_xderef:
				case DW_OP_abs:
				case DW_OP_and:
				case DW_OP_div:
				case DW_OP_minus:
				case DW_OP_mod:
				case DW_OP_mul:
				case DW_OP_neg:
				case DW_OP_not:
				case DW_OP_or:
				case DW_OP_plus:
				case DW_OP_shl:
				case DW_OP_shr:
				case DW_OP_shra:
				case DW_OP_xor:
				case DW_OP_eq:
				case DW_OP_ge:
				case DW_OP_gt:
				case DW_OP_le:
				case DW_OP_lt:
				case DW_OP_ne:
				case DW_OP_nop:
					break;

				case DW_OP_const1u:
				case DW_OP_const1s:
				case DW_OP_pick:
				case DW_OP_deref_size:
				case DW_OP_xderef_size:
				case DW_OP_const2u:
				case DW_OP_const2s:
				case DW_OP_bra:
				case DW_OP_skip:
				case DW_OP_const4u:
				case DW_OP_const4s:
				case DW_OP_const8u:
				case DW_OP_const8s:
				case DW_OP_constu:
				case DW_OP_plus_uconst:
				case DW_OP_regx:
				case DW_OP_piece:
					printf(": %ju", (uintmax_t)
					    lr->lr_number);
					break;

				case DW_OP_consts:
				case DW_OP_breg0:
				case DW_OP_breg1:
				case DW_OP_breg2:
				case DW_OP_breg3:
				case DW_OP_breg4:
				case DW_OP_breg5:
				case DW_OP_breg6:
				case DW_OP_breg7:
				case DW_OP_breg8:
				case DW_OP_breg9:
				case DW_OP_breg10:
				case DW_OP_breg11:
				case DW_OP_breg12:
				case DW_OP_breg13:
				case DW_OP_breg14:
				case DW_OP_breg15:
				case DW_OP_breg16:
				case DW_OP_breg17:
				case DW_OP_breg18:
				case DW_OP_breg19:
				case DW_OP_breg20:
				case DW_OP_breg21:
				case DW_OP_breg22:
				case DW_OP_breg23:
				case DW_OP_breg24:
				case DW_OP_breg25:
				case DW_OP_breg26:
				case DW_OP_breg27:
				case DW_OP_breg28:
				case DW_OP_breg29:
				case DW_OP_breg30:
				case DW_OP_breg31:
				case DW_OP_fbreg:
					printf(": %jd", (intmax_t)
					    lr->lr_number);
					break;

				case DW_OP_bregx:
					printf(": %ju %jd",
					    (uintmax_t) lr->lr_number,
					    (intmax_t) lr->lr_number2);
					break;

				case DW_OP_addr:
					printf(": %#jx", (uintmax_t)
					    lr->lr_number);
					break;
				}
				if (j < llbuf[i]->ld_cents - 1)
					printf(", ");
			}
			putchar(')');

			if (llbuf[i]->ld_lopc == llbuf[i]->ld_hipc)
				printf(" (start == end)");
			putchar('\n');
		}
	}
}

/*
 * Retrieve a string using string table section index and the string offset.
 */
static const char*
get_string(struct readcgcef *re, int strtab, size_t off)
{
	const char *name;

	if ((name = cgcef_strptr(re->cgcef, strtab, off)) == NULL)
		return ("");

	return (name);
}

/*
 * Retrieve the name of a symbol using the section index of the symbol
 * table and the index of the symbol within that table.
 */
static const char *
get_symbol_name(struct readcgcef *re, int symtab, int i)
{
	struct section	*s;
	const char	*name;
	GCGCEf_Sym	 sym;
	CGCEf_Data	*data;
	int		 cgceferr;

	s = &re->sl[symtab];
	if (s->type != SHT_SYMTAB && s->type != SHT_DYNSYM)
		return ("");
	(void) cgcef_errno();
	if ((data = cgcef_getdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_getdata failed: %s", cgcef_errmsg(cgceferr));
		return ("");
	}
	if (gcgcef_getsym(data, i, &sym) != &sym)
		return ("");
	/* Return section name for STT_SECTION symbol. */
	if (GCGCEF_ST_TYPE(sym.st_info) == STT_SECTION &&
	    re->sl[sym.st_shndx].name != NULL)
		return (re->sl[sym.st_shndx].name);
	if ((name = cgcef_strptr(re->cgcef, s->link, sym.st_name)) == NULL)
		return ("");

	return (name);
}

static uint64_t
get_symbol_value(struct readcgcef *re, int symtab, int i)
{
	struct section	*s;
	GCGCEf_Sym	 sym;
	CGCEf_Data	*data;
	int		 cgceferr;

	s = &re->sl[symtab];
	if (s->type != SHT_SYMTAB && s->type != SHT_DYNSYM)
		return (0);
	(void) cgcef_errno();
	if ((data = cgcef_getdata(s->scn, NULL)) == NULL) {
		cgceferr = cgcef_errno();
		if (cgceferr != 0)
			warnx("cgcef_getdata failed: %s", cgcef_errmsg(cgceferr));
		return (0);
	}
	if (gcgcef_getsym(data, i, &sym) != &sym)
		return (0);

	return (sym.st_value);
}

static void
hex_dump(struct readcgcef *re)
{
	struct section *s;
	CGCEf_Data *d;
	uint8_t *buf;
	size_t sz, nbytes;
	uint64_t addr;
	int cgceferr, i, j;

	for (i = 1; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (find_dumpop(re, (size_t) i, s->name, HEX_DUMP, -1) == NULL)
			continue;
		(void) cgcef_errno();
		if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
			cgceferr = cgcef_errno();
			if (cgceferr != 0)
				warnx("cgcef_getdata failed: %s",
				    cgcef_errmsg(cgceferr));
			continue;
		}
		if (d->d_size <= 0 || d->d_buf == NULL) {
			printf("\nSection '%s' has no data to dump.\n",
			    s->name);
			continue;
		}
		buf = d->d_buf;
		sz = d->d_size;
		addr = s->addr;
		printf("\nHex dump of section '%s':\n", s->name);
		while (sz > 0) {
			printf("  0x%8.8jx ", (uintmax_t)addr);
			nbytes = sz > 16? 16 : sz;
			for (j = 0; j < 16; j++) {
				if ((size_t)j < nbytes)
					printf("%2.2x", buf[j]);
				else
					printf("  ");
				if ((j & 3) == 3)
					printf(" ");
			}
			for (j = 0; (size_t)j < nbytes; j++) {
				if (isprint(buf[j]))
					printf("%c", buf[j]);
				else
					printf(".");
			}
			printf("\n");
			buf += nbytes;
			addr += nbytes;
			sz -= nbytes;
		}
	}
}

static void
str_dump(struct readcgcef *re)
{
	struct section *s;
	CGCEf_Data *d;
	unsigned char *start, *end, *buf_end;
	unsigned int len;
	int i, j, cgceferr, found;

	for (i = 1; (size_t) i < re->shnum; i++) {
		s = &re->sl[i];
		if (find_dumpop(re, (size_t) i, s->name, STR_DUMP, -1) == NULL)
			continue;
		(void) cgcef_errno();
		if ((d = cgcef_getdata(s->scn, NULL)) == NULL) {
			cgceferr = cgcef_errno();
			if (cgceferr != 0)
				warnx("cgcef_getdata failed: %s",
				    cgcef_errmsg(cgceferr));
			continue;
		}
		if (d->d_size <= 0 || d->d_buf == NULL) {
			printf("\nSection '%s' has no data to dump.\n",
			    s->name);
			continue;
		}
		buf_end = (unsigned char *) d->d_buf + d->d_size;
		start = (unsigned char *) d->d_buf;
		found = 0;
		printf("\nString dump of section '%s':\n", s->name);
		for (;;) {
			while (start < buf_end && !isprint(*start))
				start++;
			if (start >= buf_end)
				break;
			end = start + 1;
			while (end < buf_end && isprint(*end))
				end++;
			printf("  [%6lx]  ",
			    (long) (start - (unsigned char *) d->d_buf));
			len = end - start;
			for (j = 0; (unsigned int) j < len; j++)
				putchar(start[j]);
			putchar('\n');
			found = 1;
			if (end >= buf_end)
				break;
			start = end + 1;
		}
		if (!found)
			printf("  No strings found in this section.");
		putchar('\n');
	}
}

static void
load_sections(struct readcgcef *re)
{
	struct section	*s;
	const char	*name;
	CGCEf_Scn		*scn;
	GCGCEf_Shdr	 sh;
	size_t		 shstrndx, ndx;
	int		 cgceferr;

	/* Allocate storage for internal section list. */
	if (!(cgcef_getshdrnum(re->cgcef, &re->shnum) >= 0)) {
		warnx("cgcef_getshnum failed: %s", cgcef_errmsg(-1));
		return;
	}
	if (re->sl != NULL)
		free(re->sl);
	if ((re->sl = calloc(re->shnum, sizeof(*re->sl))) == NULL)
		err(EXIT_FAILURE, "calloc failed");

	/* Get the index of .shstrtab section. */
	if (!(cgcef_getshdrstrndx(re->cgcef, &shstrndx) >= 0)) {
		warnx("cgcef_getshstrndx failed: %s", cgcef_errmsg(-1));
		return;
	}

	if ((scn = cgcef_getscn(re->cgcef, 0)) == NULL) {
		warnx("cgcef_getscn failed: %s", cgcef_errmsg(-1));
		return;
	}

	(void) cgcef_errno();
	do {
		if (gcgcef_getshdr(scn, &sh) == NULL) {
			warnx("gcgcef_getshdr failed: %s", cgcef_errmsg(-1));
			(void) cgcef_errno();
			continue;
		}
		if ((name = cgcef_strptr(re->cgcef, shstrndx, sh.sh_name)) == NULL) {
			(void) cgcef_errno();
			name = "ERROR";
		}
		if ((ndx = cgcef_ndxscn(scn)) == SHN_UNDEF) {
			if ((cgceferr = cgcef_errno()) != 0)
				warnx("cgcef_ndxscn failed: %s",
				    cgcef_errmsg(cgceferr));
			continue;
		}
		if (ndx >= re->shnum) {
			warnx("section index of '%s' out of range", name);
			continue;
		}
		s = &re->sl[ndx];
		s->name = name;
		s->scn = scn;
		s->off = sh.sh_offset;
		s->sz = sh.sh_size;
		s->entsize = sh.sh_entsize;
		s->align = sh.sh_addralign;
		s->type = sh.sh_type;
		s->flags = sh.sh_flags;
		s->addr = sh.sh_addr;
		s->link = sh.sh_link;
		s->info = sh.sh_info;
	} while ((scn = cgcef_nextscn(re->cgcef, scn)) != NULL);
	cgceferr = cgcef_errno();
	if (cgceferr != 0)
		warnx("cgcef_nextscn failed: %s", cgcef_errmsg(cgceferr));
}

static void
unload_sections(struct readcgcef *re)
{

	if (re->sl != NULL) {
		free(re->sl);
		re->sl = NULL;
	}
	re->shnum = 0;
	re->vd_s = NULL;
	re->vn_s = NULL;
	re->vs_s = NULL;
	re->vs = NULL;
	re->vs_sz = 0;
	if (re->ver != NULL) {
		free(re->ver);
		re->ver = NULL;
		re->ver_sz = 0;
	}
}

static void
dump_cgcef(struct readcgcef *re)
{

	/* Fetch CGCEF header. No need to continue if it fails. */
	if (gcgcef_getehdr(re->cgcef, &re->ehdr) == NULL) {
		warnx("gcgcef_getehdr failed: %s", cgcef_errmsg(-1));
		return;
	}
	if ((re->ec = gcgcef_getclass(re->cgcef)) == CGCEFCLASSNONE) {
		warnx("gcgcef_getclass failed: %s", cgcef_errmsg(-1));
		return;
	}
	if (re->ehdr.e_ident[EI_DATA] == CGCEFDATA2MSB) {
		re->dw_read = _read_msb;
		re->dw_decode = _decode_msb;
	} else {
		re->dw_read = _read_lsb;
		re->dw_decode = _decode_lsb;
	}

	if (re->options & ~RE_H)
		load_sections(re);
	if ((re->options & RE_VV) || (re->options & RE_S))
		search_ver(re);
	if (re->options & RE_H)
		dump_ehdr(re);
	if (re->options & RE_L)
		dump_phdr(re);
	if (re->options & RE_SS)
		dump_shdr(re);
	if (re->options & RE_D)
		dump_dynamic(re);
	if (re->options & RE_R)
		dump_reloc(re);
	if (re->options & RE_S)
		dump_symtabs(re);
#if 0
	if (re->options & RE_N)
		dump_notes(re);
#endif
	if (re->options & RE_N)
#if 0
	if (re->options & RE_II)
		dump_hash(re);
#endif
	if (re->options & RE_X)
		hex_dump(re);
	if (re->options & RE_P)
		str_dump(re);
	if (re->options & RE_VV)
		dump_ver(re);
	if (re->options & RE_AA)
		dump_arch_specific_info(re);
	if (re->options & RE_W)
		dump_dwarf(re);
	if (re->options & ~RE_H)
		unload_sections(re);
}

static void
dump_dwarf(struct readcgcef *re)
{
	int error;
	Dwarf_Error de;

	if (dwarf_cgcef_init(re->cgcef, DW_DLC_READ, NULL, NULL, &re->dbg, &de)) {
		if ((error = dwarf_errno(de)) != DW_DLE_DEBUG_INFO_NULL)
			errx(EXIT_FAILURE, "dwarf_cgcef_init failed: %s",
			    dwarf_errmsg(de));
		return;
	}

	if (re->dop & DW_A)
		dump_dwarf_abbrev(re);
	if (re->dop & DW_L)
		dump_dwarf_line(re);
	if (re->dop & DW_LL)
		dump_dwarf_line_decoded(re);
	if (re->dop & DW_I)
		dump_dwarf_info(re);
	if (re->dop & DW_P)
		dump_dwarf_pubnames(re);
	if (re->dop & DW_R)
		dump_dwarf_aranges(re);
	if (re->dop & DW_RR)
		dump_dwarf_ranges(re);
	if (re->dop & DW_M)
		dump_dwarf_macinfo(re);
	if (re->dop & DW_F)
		dump_dwarf_frame(re, 0);
	else if (re->dop & DW_FF)
		dump_dwarf_frame(re, 1);
	if (re->dop & DW_S)
		dump_dwarf_str(re);
	if (re->dop & DW_O)
		dump_dwarf_loclist(re);

	dwarf_finish(re->dbg, &de);
}

static void
dump_ar(struct readcgcef *re, int fd)
{
	CGCEf_Arsym *arsym;
	CGCEf_Arhdr *arhdr;
	CGCEf_Cmd cmd;
	CGCEf *e;
	size_t sz;
	off_t off;
	int i;

	re->ar = re->cgcef;

	if (re->options & RE_C) {
		if ((arsym = cgcef_getarsym(re->ar, &sz)) == NULL) {
			warnx("cgcef_getarsym() failed: %s", cgcef_errmsg(-1));
			goto process_members;
		}
		printf("Index of archive %s: (%ju entries)\n", re->filename,
		    (uintmax_t) sz - 1);
		off = 0;
		for (i = 0; (size_t) i < sz; i++) {
			if (arsym[i].as_name == NULL)
				break;
			if (arsym[i].as_off != (size_t)off) {
				off = arsym[i].as_off;
				if (cgcef_rand(re->ar, off) != (size_t)off) {
					warnx("cgcef_rand() failed: %s",
					    cgcef_errmsg(-1));
					continue;
				}
				if ((e = cgcef_begin(fd, CGCEF_C_READ, re->ar)) ==
				    NULL) {
					warnx("cgcef_begin() failed: %s",
					    cgcef_errmsg(-1));
					continue;
				}
				if ((arhdr = cgcef_getarhdr(e)) == NULL) {
					warnx("cgcef_getarhdr() failed: %s",
					    cgcef_errmsg(-1));
					cgcef_end(e);
					continue;
				}
				printf("Binary %s(%s) contains:\n",
				    re->filename, arhdr->ar_name);
			}
			printf("\t%s\n", arsym[i].as_name);
		}
		if (cgcef_rand(re->ar, SARMAG) != SARMAG) {
			warnx("cgcef_rand() failed: %s", cgcef_errmsg(-1));
			return;
		}
	}

process_members:

	if ((re->options & ~RE_C) == 0)
		return;

	cmd = CGCEF_C_READ;
	while ((re->cgcef = cgcef_begin(fd, cmd, re->ar)) != NULL) {
		if ((arhdr = cgcef_getarhdr(re->cgcef)) == NULL) {
			warnx("cgcef_getarhdr() failed: %s", cgcef_errmsg(-1));
			goto next_member;
		}
		if (strcmp(arhdr->ar_name, "/") == 0 ||
		    strcmp(arhdr->ar_name, "//") == 0 ||
		    strcmp(arhdr->ar_name, "__.SYMDEF") == 0)
			goto next_member;
		printf("\nFile: %s(%s)\n", re->filename, arhdr->ar_name);
		dump_cgcef(re);

	next_member:
		cmd = cgcef_next(re->cgcef);
		cgcef_end(re->cgcef);
	}
	re->cgcef = re->ar;
}

static void
dump_object(struct readcgcef *re)
{
	int fd;

	if ((fd = open(re->filename, O_RDONLY)) == -1) {
		warn("open %s failed", re->filename);
		return;
	}

	if ((re->flags & DISPLAY_FILENAME) != 0)
		printf("\nFile: %s\n", re->filename);

	if ((re->cgcef = cgcef_begin(fd, CGCEF_C_READ, NULL)) == NULL) {
		warnx("cgcef_begin() failed: %s", cgcef_errmsg(-1));
		return;
	}

	switch (cgcef_kind(re->cgcef)) {
	case CGCEF_K_NONE:
		warnx("Not an CGCEF file.");
		return;
	case CGCEF_K_CGCEF:
		dump_cgcef(re);
		break;
	case CGCEF_K_AR:
		dump_ar(re, fd);
		break;
	default:
		warnx("Internal: libcgcef returned unknown cgcef kind.");
		return;
	}

	cgcef_end(re->cgcef);
}

static void
add_dumpop(struct readcgcef *re, size_t si, const char *sn, int op, int t)
{
	struct dumpop *d;

	if ((d = find_dumpop(re, si, sn, -1, t)) == NULL) {
		if ((d = calloc(1, sizeof(*d))) == NULL)
			err(EXIT_FAILURE, "calloc failed");
		if (t == DUMP_BY_INDEX)
			d->u.si = si;
		else
			d->u.sn = sn;
		d->type = t;
		d->op = op;
		STAILQ_INSERT_TAIL(&re->v_dumpop, d, dumpop_list);
	} else
		d->op |= op;
}

static struct dumpop *
find_dumpop(struct readcgcef *re, size_t si, const char *sn, int op, int t)
{
	struct dumpop *d;

	STAILQ_FOREACH(d, &re->v_dumpop, dumpop_list) {
		if ((op == -1 || op & d->op) &&
		    (t == -1 || (unsigned) t == d->type)) {
			if ((d->type == DUMP_BY_INDEX && d->u.si == si) ||
			    (d->type == DUMP_BY_NAME && !strcmp(d->u.sn, sn)))
				return (d);
		}
	}

	return (NULL);
}

static struct {
	const char *ln;
	char sn;
	int value;
} dwarf_op[] = {
	{"rawline", 'l', DW_L},
	{"decodedline", 'L', DW_LL},
	{"info", 'i', DW_I},
	{"abbrev", 'a', DW_A},
	{"pubnames", 'p', DW_P},
	{"aranges", 'r', DW_R},
	{"ranges", 'r', DW_R},
	{"Ranges", 'R', DW_RR},
	{"macro", 'm', DW_M},
	{"frames", 'f', DW_F},
	{"", 'F', DW_FF},
	{"str", 's', DW_S},
	{"loc", 'o', DW_O},
	{NULL, 0, 0}
};

static void
parse_dwarf_op_short(struct readcgcef *re, const char *op)
{
	int i;

	if (op == NULL) {
		re->dop |= DW_DEFAULT_OPTIONS;
		return;
	}

	for (; *op != '\0'; op++) {
		for (i = 0; dwarf_op[i].ln != NULL; i++) {
			if (dwarf_op[i].sn == *op) {
				re->dop |= dwarf_op[i].value;
				break;
			}
		}
	}
}

static void
parse_dwarf_op_long(struct readcgcef *re, const char *op)
{
	char *p, *token, *bp;
	int i;

	if (op == NULL) {
		re->dop |= DW_DEFAULT_OPTIONS;
		return;
	}

	if ((p = strdup(op)) == NULL)
		err(EXIT_FAILURE, "strdup failed");
	bp = p;

	while ((token = strsep(&p, ",")) != NULL) {
		for (i = 0; dwarf_op[i].ln != NULL; i++) {
			if (!strcmp(token, dwarf_op[i].ln)) {
				re->dop |= dwarf_op[i].value;
				break;
			}
		}
	}

	free(bp);
}

static uint64_t
_read_lsb(CGCEf_Data *d, uint64_t *offsetp, int bytes_to_read)
{
	uint64_t ret;
	uint8_t *src;

	src = (uint8_t *) d->d_buf + *offsetp;

	ret = 0;
	switch (bytes_to_read) {
	case 8:
		ret |= ((uint64_t) src[4]) << 32 | ((uint64_t) src[5]) << 40;
		ret |= ((uint64_t) src[6]) << 48 | ((uint64_t) src[7]) << 56;
	case 4:
		ret |= ((uint64_t) src[2]) << 16 | ((uint64_t) src[3]) << 24;
	case 2:
		ret |= ((uint64_t) src[1]) << 8;
	case 1:
		ret |= src[0];
		break;
	default:
		return (0);
	}

	*offsetp += bytes_to_read;

	return (ret);
}

static uint64_t
_read_msb(CGCEf_Data *d, uint64_t *offsetp, int bytes_to_read)
{
	uint64_t ret;
	uint8_t *src;

	src = (uint8_t *) d->d_buf + *offsetp;

	switch (bytes_to_read) {
	case 1:
		ret = src[0];
		break;
	case 2:
		ret = src[1] | ((uint64_t) src[0]) << 8;
		break;
	case 4:
		ret = src[3] | ((uint64_t) src[2]) << 8;
		ret |= ((uint64_t) src[1]) << 16 | ((uint64_t) src[0]) << 24;
		break;
	case 8:
		ret = src[7] | ((uint64_t) src[6]) << 8;
		ret |= ((uint64_t) src[5]) << 16 | ((uint64_t) src[4]) << 24;
		ret |= ((uint64_t) src[3]) << 32 | ((uint64_t) src[2]) << 40;
		ret |= ((uint64_t) src[1]) << 48 | ((uint64_t) src[0]) << 56;
		break;
	default:
		return (0);
	}

	*offsetp += bytes_to_read;

	return (ret);
}

static uint64_t
_decode_lsb(uint8_t **data, int bytes_to_read)
{
	uint64_t ret;
	uint8_t *src;

	src = *data;

	ret = 0;
	switch (bytes_to_read) {
	case 8:
		ret |= ((uint64_t) src[4]) << 32 | ((uint64_t) src[5]) << 40;
		ret |= ((uint64_t) src[6]) << 48 | ((uint64_t) src[7]) << 56;
	case 4:
		ret |= ((uint64_t) src[2]) << 16 | ((uint64_t) src[3]) << 24;
	case 2:
		ret |= ((uint64_t) src[1]) << 8;
	case 1:
		ret |= src[0];
		break;
	default:
		return (0);
	}

	*data += bytes_to_read;

	return (ret);
}

static uint64_t
_decode_msb(uint8_t **data, int bytes_to_read)
{
	uint64_t ret;
	uint8_t *src;

	src = *data;

	ret = 0;
	switch (bytes_to_read) {
	case 1:
		ret = src[0];
		break;
	case 2:
		ret = src[1] | ((uint64_t) src[0]) << 8;
		break;
	case 4:
		ret = src[3] | ((uint64_t) src[2]) << 8;
		ret |= ((uint64_t) src[1]) << 16 | ((uint64_t) src[0]) << 24;
		break;
	case 8:
		ret = src[7] | ((uint64_t) src[6]) << 8;
		ret |= ((uint64_t) src[5]) << 16 | ((uint64_t) src[4]) << 24;
		ret |= ((uint64_t) src[3]) << 32 | ((uint64_t) src[2]) << 40;
		ret |= ((uint64_t) src[1]) << 48 | ((uint64_t) src[0]) << 56;
		break;
	default:
		return (0);
		break;
	}

	*data += bytes_to_read;

	return (ret);
}

static int64_t
_decode_sleb128(uint8_t **dp)
{
	int64_t ret = 0;
	uint8_t b;
	int shift = 0;

	uint8_t *src = *dp;

	do {
		b = *src++;
		ret |= ((b & 0x7f) << shift);
		shift += 7;
	} while ((b & 0x80) != 0);

	if (shift < 32 && (b & 0x40) != 0)
		ret |= (-1 << shift);

	*dp = src;

	return (ret);
}

static uint64_t
_decode_uleb128(uint8_t **dp)
{
	uint64_t ret = 0;
	uint8_t b;
	int shift = 0;

	uint8_t *src = *dp;

	do {
		b = *src++;
		ret |= ((b & 0x7f) << shift);
		shift += 7;
	} while ((b & 0x80) != 0);

	*dp = src;

	return (ret);
}

static void
readcgcef_version(void)
{
	/* KLUDGE: our libcgcef doesn't have a version function */
	(void) printf("%s (%s)\n", CGCEFTC_GETPROGNAME(), "???");
	exit(EXIT_SUCCESS);
}

#define	USAGE_MESSAGE	"\
Usage: %s [options] file...\n\
  Display information about CGCEF objects and ar(1) archives.\n\n\
  Options:\n\
  -a | --all               Equivalent to specifying options '-dhIlrsASV'.\n\
  -c | --archive-index     Print the archive symbol table for archives.\n\
  -d | --dynamic           Print the contents of SHT_DYNAMIC sections.\n\
  -e | --headers           Print all headers in the object.\n\
  -g | --section-groups    (accepted, but ignored)\n\
  -h | --file-header       Print the file header for the object.\n\
  -l | --program-headers   Print the PHDR table for the object.\n\
  -n | --notes             Print the contents of SHT_NOTE sections.\n\
  -p INDEX | --string-dump=INDEX\n\
                           Print the contents of section at index INDEX.\n\
  -r | --relocs            Print relocation information.\n\
  -s | --syms | --symbols  Print symbol tables.\n\
  -t | --section-details   Print additional information about sections.\n\
  -v | --version           Print a version identifier and exit.\n\
  -x INDEX | --hex-dump=INDEX\n\
                           Display contents of a section as hexadecimal.\n\
  -A | --arch-specific     (accepted, but ignored)\n\
  -D | --use-dynamic       Print the symbol table specified by the DT_SYMTAB\n\
                           entry in the \".dynamic\" section.\n\
  -H | --help              Print a help message.\n\
  -I | --histogram         Print information on bucket list lengths for \n\
                           hash sections.\n\
  -N | --full-section-name (accepted, but ignored)\n\
  -S | --sections | --section-headers\n\
                           Print information about section headers.\n\
  -V | --version-info      Print symbol versoning information.\n\
  -W | --wide              Print information without wrapping long lines.\n"


static void
readcgcef_usage(void)
{
	fprintf(stderr, USAGE_MESSAGE, CGCEFTC_GETPROGNAME());
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	struct readcgcef	*re, re_storage;
	unsigned long	 si;
	int		 opt, i;
	char		*ep;

	re = &re_storage;
	memset(re, 0, sizeof(*re));
	STAILQ_INIT(&re->v_dumpop);

	while ((opt = getopt_long(argc, argv, "AacDdegHhIi:lNnp:rSstuVvWw::x:",
	    longopts, NULL)) != -1) {
		switch(opt) {
		case '?':
			readcgcef_usage();
			break;
		case 'A':
			re->options |= RE_AA;
			break;
		case 'a':
			re->options |= RE_AA | RE_D | RE_H | RE_II | RE_L |
			    RE_R | RE_SS | RE_S | RE_VV;
			break;
		case 'c':
			re->options |= RE_C;
			break;
		case 'D':
			re->options |= RE_DD;
			break;
		case 'd':
			re->options |= RE_D;
			break;
		case 'e':
			re->options |= RE_H | RE_L | RE_SS;
			break;
		case 'g':
			re->options |= RE_G;
			break;
		case 'H':
			readcgcef_usage();
			break;
		case 'h':
			re->options |= RE_H;
			break;
		case 'I':
			re->options |= RE_II;
			break;
		case 'i':
			/* Not implemented yet. */
			break;
		case 'l':
			re->options |= RE_L;
			break;
		case 'N':
			re->options |= RE_NN;
			break;
		case 'n':
			re->options |= RE_N;
			break;
		case 'p':
			re->options |= RE_P;
			si = strtoul(optarg, &ep, 10);
			if (*ep == '\0')
				add_dumpop(re, (size_t) si, NULL, STR_DUMP,
				    DUMP_BY_INDEX);
			else
				add_dumpop(re, 0, optarg, STR_DUMP,
				    DUMP_BY_NAME);
			break;
		case 'r':
			re->options |= RE_R;
			break;
		case 'S':
			re->options |= RE_SS;
			break;
		case 's':
			re->options |= RE_S;
			break;
		case 't':
			re->options |= RE_T;
			break;
		case 'u':
			re->options |= RE_U;
			break;
		case 'V':
			re->options |= RE_VV;
			break;
		case 'v':
			readcgcef_version();
			break;
		case 'W':
			re->options |= RE_WW;
			break;
		case 'w':
			re->options |= RE_W;
			parse_dwarf_op_short(re, optarg);
			break;
		case 'x':
			re->options |= RE_X;
			si = strtoul(optarg, &ep, 10);
			if (*ep == '\0')
				add_dumpop(re, (size_t) si, NULL, HEX_DUMP,
				    DUMP_BY_INDEX);
			else
				add_dumpop(re, 0, optarg, HEX_DUMP,
				    DUMP_BY_NAME);
			break;
		case OPTION_DEBUG_DUMP:
			re->options |= RE_W;
			parse_dwarf_op_long(re, optarg);
		}
	}

	argv += optind;
	argc -= optind;

	if (argc == 0 || re->options == 0)
		readcgcef_usage();

	if (argc > 1)
		re->flags |= DISPLAY_FILENAME;

	if (cgcef_version(EV_CURRENT) == EV_NONE)
		errx(EXIT_FAILURE, "CGCEF library initialization failed: %s",
		    cgcef_errmsg(-1));

	for (i = 0; i < argc; i++)
		if (argv[i] != NULL) {
			re->filename = argv[i];
			dump_object(re);
		}

	exit(EXIT_SUCCESS);
}
