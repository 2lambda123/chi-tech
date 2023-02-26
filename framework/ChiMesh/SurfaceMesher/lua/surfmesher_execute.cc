#include "ChiLua/chi_lua.h"

#include "../surfacemesher.h"
#include "ChiMesh/MeshHandler/chi_meshhandler.h"

#include "chi_runtime.h"
#include "chi_log.h"

//#############################################################################
/** Executes the surface meshing pipeline.

\ingroup LuaSurfaceMesher
\author Jan*/
int chiSurfaceMesherExecute(lua_State *L)
{
  auto& cur_hndlr = chi_mesh::GetCurrentHandler();
  chi::log.LogAllVerbose2() << "Executing surface mesher\n";

  cur_hndlr.GetSurfaceMesher().Execute();

  chi::log.LogAllVerbose2()
    << "chiSurfaceMesherExecute: Surface mesher execution completed."
    << std::endl;

  return 0;
}