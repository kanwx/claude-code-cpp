#ifdef HAS_FTXUI

// FtxuiRender.cpp — previously contained detail::MainComponent.
// MainComponent has been replaced by the AppLayout component tree
// (see include/claude/ui/components/AppLayout.hpp).
//
// All rendering is now orchestrated by AppLayoutComponent, which
// composes HeaderBar, ContentArea, permission overlay, input line,
// completions, and footer into the full-screen layout.
//
// Event handling is done via CatchEvent in FtxuiRepl::BuildMainComponent(),
// not in this file.

#endif // HAS_FTXUI
