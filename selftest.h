/****************************************************************************
** Asmon — fork of SASM (GPL-3.0).
** Modifications © 2026 Asmon contributors.
****************************************************************************/
#ifndef SELFTEST_H
#define SELFTEST_H

//! "asmon --selftest": checks the editor features, returns the number of failed checks
int runSelfTest(int argc, char **argv);
//! collect Qt warnings from the start (the style sheet is parsed early)
void installSelfTestMessageHandler();

#endif
