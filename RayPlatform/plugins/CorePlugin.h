/*
 	Ray
    Copyright (C) 2010, 2011, 2012  Sébastien Boisvert

	http://DeNovoAssembler.SourceForge.Net/

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, version 3 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You have received a copy of the GNU Lesser General Public License
    along with this program (lgpl-3.0.txt).  
	see <http://www.gnu.org/licenses/>

*/

#ifndef _CorePlugin_h
#define _CorePlugin_h

#include <core/types.h>

/** get the static name for the variable **/
#define __GetPlugin(corePlugin) \
	staticPlugin_ ## corePlugin

/** get the method name in the plugin **/
#define __GetMethod(handle) \
	call_ ## handle

/** get the function pointer name for the adapter **/
#define __GetAdapter(corePlugin,handle) \
	Adapter_ ## corePlugin ## _call_ ## handle 

/* create the static core pluging thing. */
#define __CreatePlugin( corePlugin ) \
	static corePlugin * __GetPlugin( corePlugin ) ;

#define __BindPlugin( corePlugin ) \
	__GetPlugin( corePlugin ) = this;

class ComputeCore;

/** 
 * In the Ray Platform, plugins are registered onto the
 * core (ComputeCore).
 * The only method of this interface is to register the plugin.
 */
class CorePlugin{

protected:

	PluginHandle m_plugin;

public:

/** register the plugin **/
	virtual void registerPlugin(ComputeCore*computeCore);

/** resolve the symbols **/
	virtual void resolveSymbols(ComputeCore*core);

	virtual ~CorePlugin();
};

#endif

