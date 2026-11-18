#!/bin/sh
# Builds the Ghidra decompiler (C++ only, no Java, no libbfd) and the SLEIGH compiler,
# compiles the x86-64 processor description, and lays out the runtime files.
#   usage: build.sh <ghidra source dir> <output dir> [make -j jobs]
# Ghidra is Apache-2.0 (see THIRD_PARTY.md). The generated parsers are part of the Ghidra source tree.
set -e
G="$1"; OUT="$2"; JOBS="${3:-4}"
CPP="$G/Ghidra/Features/Decompiler/src/decompile/cpp"
LANGS="$G/Ghidra/Processors/x86/data/languages"
mkdir -p "$OUT/obj" "$OUT/bin" "$OUT/Ghidra/Processors/x86/data/languages"
cd "$CPP"
CORE="xml marshal space float address pcoderaw translate opcodes metatype globalcontext"
DECCORE="capability architecture options graph cover block cast typeop database cpool comment stringmanage modelrules fspec action loadimage grammar varnode op type variable varmap jumptable emulate emulateutil flow userop expression multiprecision funcdata funcdata_block funcdata_op funcdata_varnode unionresolve pcodeinject heritage prefersplit rangeutil ruleaction subflow blockaction merge double transform constseq bitfield coreaction condexe override dynamic crc32 prettyprint printlanguage printc printjava memstate opbehavior paramid signature"
SLEIGH="sleigh pcodeparse pcodecompile sleighbase slghsymbol slghpatexpress slghpattern semantics context slaformat compression filemanage"
SLACOMP="slgh_compile slghparse slghscan"
GHIDRA="ghidra_arch inject_ghidra ghidra_translate loadimage_ghidra typegrp_ghidra database_ghidra ghidra_context cpool_ghidra ghidra_process comment_ghidra string_ghidra signature_ghidra"
SPECIAL="consolemain sleighexample test"
SKIP="analyzesigs bfd_arch codedata loadimage_bfd"
EXTRA=""
for f in *.cc; do
  n="${f%.cc}"; skip=0
  for s in $CORE $DECCORE $SLEIGH $SLACOMP $GHIDRA $SPECIAL $SKIP; do [ "$s" = "$n" ] && skip=1; done
  case "$n" in ghidraext_*|coreext_*|consoleext_*) skip=1;; esac
  [ $skip = 0 ] && EXTRA="$EXTRA $n"
done
CXX="${CXX:-g++}"
LDX="${LDX:-}"          # e.g. -static (Windows: no runtime DLLs next to the program)
FLAGS="-std=c++11 -O2 -w -I."
case "$($CXX -dumpmachine)" in *mingw*|*msys*) FLAGS="$FLAGS -D_WINDOWS";; esac   # Ghidra's name for the Win32 code paths
# mingw's <limits.h> defines INT_MIN as a macro, which breaks a local constant of the same name in opbehavior.cc
sed -i 's/\bINT_MIN\b/INTB_MIN_/g' opbehavior.cc
# the flex scanner defines its own isatty() on Windows, which clashes with mingw's <io.h>
sed -i '/^static int isatty (int fildes) { return 0; }/d' slghscan.cc
build() { # name defines
  [ -f "$OUT/obj/$2$1.o" ] || echo "$CXX $FLAGS $3 -c $1.cc -o $OUT/obj/$2$1.o"
}
rm -f "$OUT/jobs.txt"
for n in $CORE $DECCORE $EXTRA $SLEIGH consolemain; do build $n d_ "" >> "$OUT/jobs.txt"; done
for n in $CORE $SLEIGH $SLACOMP; do build $n s_ "" >> "$OUT/jobs.txt"; done
xargs -P "$JOBS" -I{} sh -c '{}' < "$OUT/jobs.txt"
$CXX -O2 $LDX -o "$OUT/bin/asmon-decomp" $(for n in $CORE $DECCORE $EXTRA $SLEIGH consolemain; do echo "$OUT/obj/d_$n.o"; done) -lz
$CXX -O2 $LDX -o "$OUT/bin/asmon-sleigh" $(for n in $CORE $SLEIGH $SLACOMP; do echo "$OUT/obj/s_$n.o"; done) -lz
cd "$LANGS"
D="$OUT/Ghidra/Processors/x86/data/languages"
"$OUT/bin/asmon-sleigh" x86-64.slaspec "$D/x86-64.sla"
"$OUT/bin/asmon-sleigh" x86.slaspec "$D/x86.sla"
for f in x86.ldefs x86.pspec x86-64.pspec x86-64-win.cspec x86-64-gcc.cspec x86win.cspec x86gcc.cspec; do cp "$f" "$D/"; done
echo built
