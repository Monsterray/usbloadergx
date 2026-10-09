#ifndef HBCAGENT_H_
#define HBCAGENT_H_

//! The Homebrew Channel's in-app agent (hbc_agent.h, built by deps/build.sh):
//! hbc.py on TCP 4299, crash and hang reports, and the HOME overlay.

//! Starts the agent; once, after the start-up IOS reloads.
void HbcAgent_Init(void);
//! Opens the HOME overlay. False when it cannot open (no memory, or the agent
//! is stopped); the caller then shows GX's own HOME menu.
bool HbcAgent_Home(void);

#endif
