# NMake Makefile snippet for copying the built libraries, utilities and headers to
# a path under $(PREFIX).

install: all
	@if not exist $(PREFIX)\bin\ mkdir $(PREFIX)\bin
	@if not exist $(PREFIX)\lib\ mkdir $(PREFIX)\lib
	@if not exist $(PREFIX)\include\wing\ mkdir $(PREFIX)\include\wing
	@copy /b $(WING_DLL_FILENAME).dll $(PREFIX)\bin
	@copy /b $(WING_DLL_FILENAME).pdb $(PREFIX)\bin
	@copy /b $(CFG)\$(PLAT)\wing.lib $(PREFIX)\lib
	@for %h in ($(WING_ACTUAL_HEADERS)) do @copy %h $(PREFIX)\include\wing
