// For conditions of distribution and use, see copyright notice in license.txt

#ifndef incl_PythonScriptingModule_h
#define incl_PythonScriptingModule_h

#include "Foundation.h"
#include "StableHeaders.h"
#include "ModuleInterface.h"
#include "ComponentRegistrarInterface.h"
#include "ServiceManager.h"

#ifdef PYTHON_FORCE_RELEASE_VERSION
  #ifdef _DEBUG
    #undef _DEBUG
    #include "Python.h"
    #define _DEBUG
  #else
    #include "Python.h"
  #endif 
#else
    #include "Python.h"
#endif


namespace Foundation
{
    class Framework;
}

namespace RexLogic
{
    class RexLogicModule;
    class EC_OpenSimPrim;
	class CameraControllable;
	class AvatarControllable;
}

namespace PythonScript
{
     //hack to have a ref to framework so can get the module in api funcs
    static Foundation::Framework *staticframework;

    // Category id for scene events - outside the module class 'cause entity_setattro wants this too
    static Core::event_category_id_t scene_event_category_ ;

    class PythonEngine;
    typedef boost::shared_ptr<PythonEngine> PythonEnginePtr;	

    //! A scripting module using Python
    class MODULE_API PythonScriptModule : public Foundation::ModuleInterfaceImpl
    {
    public:
        PythonScriptModule();
        virtual ~PythonScriptModule();

		//the module interface
		virtual void Load();
        virtual void Unload();
        virtual void Initialize();
        virtual void PostInitialize();
        virtual void Uninitialize();
        virtual void Update(Core::f64 frametime);

		//handling events
		virtual bool HandleEvent(
			Core::event_category_id_t category_id,
            Core::event_id_t event_id, 
            Foundation::EventDataInterface* data);

		//! callback for console command        
		Console::CommandResult ConsoleRunString(const Core::StringVector &params);
        Console::CommandResult ConsoleRunFile(const Core::StringVector &params);
        Console::CommandResult ConsoleReset(const Core::StringVector &params);
        
        MODULE_LOGGING_FUNCTIONS

        //! returns name of this module. Needed for logging.
        static const std::string &NameStatic() { return Foundation::Module::NameFromType(type_static_); }
        static const Foundation::Module::Type type_static_ = Foundation::Module::MT_PythonScript;

		static Foundation::Framework* GetStaticFramework() { return PythonScript::staticframework; }
		static Foundation::ScriptEventInterface* engineAccess;

		//api code is outside the module now, but reuses these .. err, but can't see 'cause dont have a ref to the instance?
		// Category id for incoming messages.
		Core::event_category_id_t inboundCategoryID_;
		Core::event_category_id_t inputeventcategoryid;

		
	private:
        
		PythonEnginePtr engine_;
		
		//basic feats
		void RunString(const char* codestr);
		void RunFile(const std::string &modulename);
		void Reset();

		//a testing place
		void x();
		
		PyObject *apiModule; //the module made here that exposes the c++ side / api, 'rexviewer'

		// the hook to the python-written module manager that passes events on
		
		PyObject *pmmModule, *pmmDict, *pmmClass, *pmmInstance;
		PyObject *pmmArgs, *pmmValue;

		//Foundation::ScriptObject* modulemanager;
		
		// can't get passing __VA_ARGS__ to pass my args 
		//   in PythonScriptObject::CallMethod2
		//   so reverting to use the Py C API directly, not using the ScriptObject now
		//   for the modulemanager 
		

	};

	static PyObject* initpymod();
	//static Scene::ScenePtr GetScene();

	//a helper and to avoid copy-paste when doing the get in Entity.getattro
	static Scene::ScenePtr GetScene() 
	{
		//PythonScript::PythonScriptModule::LogDebug("Getting scene..");

		//in this way, the staticframework pointer gotten is 0 also in GetEntity
		//Foundation::Framework *framework_ = PythonScript::PythonScriptModule::GetStaticFramework(); 

		//this works when GetEntity calls this, but not anymore when entity_getattro does.
		Foundation::Framework *framework_ = PythonScript::staticframework;

		Scene::ScenePtr scene = framework_->GetScene("World"); //XXX hardcoded scene name, like in debugstats now
		//Scene::ScenePtr scene = rexlogicmodule_->GetCurrentActiveScene(); //this seems to have appeared, change to this XXX
		return scene;
	}
}

#endif
