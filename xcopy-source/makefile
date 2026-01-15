
# makefile XCopy Multi/US

xcopy: xio.s xcop.s xcopy.i gfx/xcopypic.pp
	OMA xio.s

gfx/xcopypic.pp: gfx/xcopypic.iff
	iff2map gfx/xcopypic.iff gfx/xcopypic.map -r
	crunch gfx/xcopypic.map gfx/xcopypic.pp eff=1
	preppp gfx/xcopypic.pp
	OMA xio.s
