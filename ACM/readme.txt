Building this codec needs nothing that Visual Studio does not already install. The
Windows SDK supplies MSAcm.h, and the driver-side msacmdrv.h is in the ddk folder
next to this file; the project puts that folder on the include path, so the header
is used whether or not a DDK is installed.

Build it from the vs_lame_clients.slnx solution in the vc_solution folder.



---------------

To release this codec you will need :
- lameACM.acm (result of the build process)
- lameACM.inf
- lame_acm.xml (where the initial configuration is stored)
