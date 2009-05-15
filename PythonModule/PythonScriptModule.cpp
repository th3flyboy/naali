// For conditions of distribution and use, see copyright notice in license.txt

#include "StableHeaders.h"
#include "PythonScriptModule.h"
//#include "Foundation.h"
#include "ServiceManager.h"
#include "ComponentRegistrarInterface.h"
#include "ConsoleCommandServiceInterface.h"
#include "PythonEngine.h" //is this needed here?

#include "RexLogicModule.h" //now for SendChat, perhaps other stuff for the api will be here too?
#include "OpenSimProtocolModule.h" //for handling net events
#include "RexProtocolMsgIDs.h"

#include "SceneEvents.h" //sending scene events after (placeable component) manipulation


#include "Entity.h"

namespace PythonScript
{
	Foundation::ScriptEventInterface* PythonScriptModule::engineAccess;// for reaching engine from static method

	PythonScriptModule::PythonScriptModule() : ModuleInterfaceImpl(type_static_)
    {
    }

    PythonScriptModule::~PythonScriptModule()
    {
    }

    // virtual
    void PythonScriptModule::Load()
    {
        using namespace PythonScript;

        LogInfo("Module " + Name() + " loaded.");
        //DECLARE_MODULE_EC(EC_OgreEntity);

        AutoRegisterConsoleCommand(Console::CreateCommand(
            "PyExec", "Execute given code in the embedded Python interpreter. Usage: PyExec(mycodestring)", 
            Console::Bind(this, &PythonScriptModule::ConsoleRunString))); 
		/* NOTE: called 'exec' cause is similar to py shell builtin exec() func.
		 * Also in the IPython shell 'run' refers to running an external file and not the given string
		 */

        AutoRegisterConsoleCommand(Console::CreateCommand(
            "PyLoad", "Execute a python file. PyLoad(mypymodule)", 
            Console::Bind(this, &PythonScriptModule::ConsoleRunFile))); 

		AutoRegisterConsoleCommand(Console::CreateCommand(
            "PyReset", "Resets the Python interpreter - should free all it's memory, and clear all state.", 
            Console::Bind(this, &PythonScriptModule::ConsoleReset))); 

    }

    // virtual
    void PythonScriptModule::Unload()
    {
        LogInfo("Module " + Name() + " unloaded.");
    }

    // virtual
    void PythonScriptModule::Initialize()
    {
		engine_ = PythonScript::PythonEnginePtr(new PythonScript::PythonEngine(framework_));
        engine_->Initialize();
		
		PythonScriptModule::engineAccess = dynamic_cast<Foundation::ScriptEventInterface*>(engine_.get());
        
        framework_->GetServiceManager()->RegisterService(Foundation::Service::ST_Scripting, engine_);

		//XXX hack to have a ref to framework for api funcs
		PythonScript::staticframework = framework_;
		PythonScript::initpymod(); //initializes the rexviewer module to be imported within py

		//XXX hardcoded loading of the test chat event handler,
		//the system for defining what to load and how they register to be designed
		std::string error;
		chathandler = engine_->LoadScript("chathandler", error); //the module
		chathandler = chathandler->GetObject("ChatHandler"); //instanciates a class in this module with the given name

        LogInfo("Module " + Name() + " initialized.");
    }

    void PythonScriptModule::PostInitialize()
    {
		//(was) for onChat
		inboundCategoryID_ = framework_->GetEventManager()->QueryEventCategory("OpenSimNetworkIn");
        if (inboundCategoryID_ == 0)
            LogWarning("Unable to find event category for incoming OpenSimNetwork events!");

		//for notifying placeable movement
		PythonScript::scene_event_category_ = framework_->GetEventManager()->QueryEventCategory("Scene");
	    if (PythonScript::scene_event_category_ == 0)
			LogWarning("Unable to find event category for Scene events!");    
	}

    bool PythonScriptModule::HandleEvent(
        Core::event_category_id_t category_id,
        Core::event_id_t event_id, 
        Foundation::EventDataInterface* data)
    {
        if (category_id == inboundCategoryID_)
        {
            OpenSimProtocol::NetworkEventInboundData *event_data = static_cast<OpenSimProtocol::NetworkEventInboundData *>(data);
            const NetMsgID msgID = event_data->messageID;
            NetInMessage *msg = event_data->message;
            const NetMessageInfo *info = event_data->message->GetMessageInfo();
            assert(info);
            
            std::stringstream ss;
            ss << info->name << " received, " << Core::ToString(msg->GetDataSize()) << " bytes.";
			//LogInfo(ss.str());

            switch(msgID)
		    {
		    case RexNetMsgChatFromSimulator:
		        {
	            std::stringstream ss;
	            size_t bytes_read = 0;

	            std::string name = (const char *)msg->ReadBuffer(&bytes_read);
	            msg->SkipToFirstVariableByName("Message");
	            std::string message = (const char *)msg->ReadBuffer(&bytes_read);
				
	            ss << "[" << Core::GetLocalTimeString() << "] " << name << ": " << message << std::endl;
				LogInfo(ss.str());
	            //WriteToChatWindow(ss.str());
				//can readbuffer ever return null? should be checked if yes. XXX

				/* reusing the PythonScriptObject made for comms.
				 * the previous method of doing pArgs and PyString_FromString certainly seems safer,
				 * we must refactor to get this other solution to use those. */
				char** args = new char*[2]; //is this 2 'cause the latter terminates?
				char* buf1 = new char[1000];
				strcpy(buf1, message.c_str());
				args[0] = buf1;

				std::string methodname = "onChat";
				std::string paramtypes = "s";
				Foundation::ScriptObject* ret = chathandler->CallMethod(methodname, paramtypes, args);

				/* previous method of calling a function with an argument tuple
	            pArgs = PyTuple_New(1); //takes a single argument
				//pValue = PyInt_FromLong(1); //..which is now just int 1
				pValue = PyString_FromString(message.c_str());
				//pValue reference stolen here:
				PyTuple_SetItem(pArgs, 0, pValue);

				pValue = PyObject_CallObject(pFunc, pArgs);
				Py_DECREF(pArgs);
				if (pValue != NULL) {
					//printf("Result of call: %ld\n", PyInt_AsLong(pValue));
					LogInfo("Python chathandler executed ok.");
					Py_DECREF(pValue);
				}
				else {
					Py_DECREF(pFunc);
					PyErr_Print();
					fprintf(stderr,"Call failed\n");
				}*/

	            break;
		        }
			}
		}

		return false;
	}

    Console::CommandResult PythonScriptModule::ConsoleRunString(const Core::StringVector &params)
	{
		if (params.size() != 1)
		{			
			return Console::ResultFailure("Usage: PyExec(print 1 + 1)");
			//how to handle input like this? PyExec(print '1 + 1 = %d' % (1 + 1))");
			//probably better have separate py shell.
		}

		else
		{
			engine_->RunString(params[0]);
			return Console::ResultSuccess();
		}
	}

    Console::CommandResult PythonScriptModule::ConsoleRunFile(const Core::StringVector &params)
	{		
		if (params.size() != 1)
		{			
			return Console::ResultFailure("Usage: PyLoad(mypymodule) (to run mypymodule.py by importing it)");
		}

		else
		{
			engine_->RunScript(params[0]);
			return Console::ResultSuccess();
		}
	}

	Console::CommandResult PythonScriptModule::ConsoleReset(const Core::StringVector &params)
	{
		engine_->Reset();
		return Console::ResultSuccess();
	}	

    // virtual 
    void PythonScriptModule::Uninitialize()
    {        
        framework_->GetServiceManager()->UnregisterService(engine_);

		engine_->Uninitialize();
        LogInfo("Module " + Name() + " uninitialized.");
    }
    
    // virtual
    void PythonScriptModule::Update(Core::f64 frametime)
    {
		//XXX remove when/as the core has the fps limitter
		engine_->RunString("import time; time.sleep(0.01);"); //a hack to save cpu now.
    }
}

extern "C" void POCO_LIBRARY_API SetProfiler(Foundation::Framework *framework);
void SetProfiler(Foundation::Framework *framework)
{
    assert (framework);
    assert (Foundation::ProfilerSection::GetProfiler() == NULL);
    Foundation::ProfilerSection::SetProfiler(&framework->GetProfiler());
}

using namespace PythonScript;

POCO_BEGIN_MANIFEST(Foundation::ModuleInterface)
   POCO_EXPORT_CLASS(PythonScriptModule)
POCO_END_MANIFEST

#ifdef __cplusplus
extern "C"
#endif

/* API calls exposed to py. 
will probably be wrapping the actual modules in separate files,
but first test now here. also will use boostpy or something, but now first by hand */
static PyObject* SendChat(PyObject *self, PyObject *args)
{
	const char* msg;

	if(!PyArg_ParseTuple(args, "s", &msg))
		return NULL;

	//Foundation::Framework *framework_ = Foundation::ComponentInterfacePythonScriptModule::GetFramework();
	Foundation::Framework *framework_ = PythonScript::staticframework;
	//todo weak_pointerize

	//move decl to .h and getting to Initialize (see NetTEstLogicModule::Initialize)
	//if this kind of usage, i.e. getting the logic module for the api, is to remain.
	RexLogic::RexLogicModule *rexlogic_;
	rexlogic_ = dynamic_cast<RexLogic::RexLogicModule *>(framework_->GetModuleManager()->GetModule(Foundation::Module::MT_WorldLogic).lock().get());

	rexlogic_->GetServerConnection()->SendChatFromViewerPacket(msg);

	Py_RETURN_TRUE;
}

//now just gives the id back, was to test using a service - w.i.p. code
static PyObject* GetEntity(PyObject *self, PyObject *args)
{
	unsigned int ent_id_int;
	Core::entity_id_t ent_id;

	PyObject* ret;

	if(!PyArg_ParseTuple(args, "i", &ent_id_int))
		return NULL; //XXX report ArgumentException error

	ent_id = (Core::entity_id_t) ent_id_int;

	Scene::ScenePtr scene = PythonScript::GetScene();

	if (scene == 0)
		return NULL; //XXX return some sensible exception info

	//PythonScript::foo(); 
	/*
	const Scene::EntityPtr entity = scene->GetEntity(ent_id);
	if (entity.get() != 0) //same that scene->HasEntity does, i.e. it first does GetEntity too, so not calling HasEntity here to not do GetEntity twice.
		return entity_create(ent_id, entity);
	//the ptr is stored in a separate map now, so also id needs to passed to entity_create
	*/
	//if would just store the id and always re-get ptr, would do this:
	if (scene->HasEntity(ent_id))
		return entity_create(ent_id);

	else
		return NULL; //XXX TODO: raise ValueError
}

static PyObject* PyEventCallback(PyObject *self, PyObject *args){
	std::cout << "PyEventCallback" << std::endl;
	const char* key;
	const char* message;
	if(!PyArg_ParseTuple(args, "ss", &key, &message))
		Py_RETURN_FALSE;
	std::cout << key << std::endl;
	std::cout << message << std::endl;
	std::string k(key);
	std::string m(message);
	PythonScript::PythonScriptModule::engineAccess->NotifyScriptEvent(k, m);
	Py_RETURN_TRUE;
}

static PyMethodDef EmbMethods[] = {
	{"sendChat", (PyCFunction)SendChat, METH_VARARGS,
	"Send the given text as a chat message."},

	{"getEntity", (PyCFunction)GetEntity, METH_VARARGS,
	"Gets the entity with the given ID."},

	{"pyEventCallback", (PyCFunction)PyEventCallback, METH_VARARGS,
	"Handling callbacks from py scripts. Calling convension: with 2 strings"},

	{NULL, NULL, 0, NULL}
};

static void PythonScript::initpymod()
{
	PyObject* m;
	
	m = Py_InitModule("rexviewer", EmbMethods);

	entity_init(m); 
	/* this is planned to be vice versa: 
	   the implementing modules, like here scene for Entity,
	   would call something here to get a ref to the module, or something?
	*/
}

/* this belongs to Entity.cpp but when added to the api from there, the staticframework is always null */
PyObject* PythonScript::entity_getattro(PyObject *self, PyObject *name)
{
	PyObject* tmp;

	if (!(tmp = PyObject_GenericGetAttr((PyObject*)self, name))) {
		if (!PyErr_ExceptionMatches(PyExc_AttributeError))
			return NULL;
		PyErr_Clear();
	}
	else
		return tmp;

	const char* c_name = PyString_AsString(name);
	std::string s_name = std::string(c_name);

	std::cout << "Entity: getting unknown attribute: " << s_name;
	
	//entity_ptrs map usage
	/* this crashes now in boost, 
	   void add_ref_copy() { BOOST_INTERLOCKED_INCREMENT( &use_count_ );
	std::map<Core::entity_id_t, Scene::EntityPtr>::iterator ep_iter = entity_ptrs.find(self->ent_id);
	Scene::EntityPtr entity = ep_iter->second;
	fix.. */

	/* re-getting the EntityPtr as it wasn't stored anywhere yet,
	   is copy-paste from PythonScriptModule GetEntity 
	   but to be removed when that map is used above.*/
	Scene::ScenePtr scene = PythonScript::GetScene();
	rexviewer_EntityObject *eob = (rexviewer_EntityObject *)self;
	Scene::EntityPtr entity = scene->GetEntity(eob->ent_id);
	
	if (s_name.compare("prim") == 0)
	{
		std::cout << ".. getting prim" << std::endl;
		const Foundation::ComponentInterfacePtr &prim_component = entity->GetComponent("EC_OpenSimPrim");
		if (!prim_component)
			return NULL; //XXX report AttributeError
		RexLogic::EC_OpenSimPrim *prim = checked_static_cast<RexLogic::EC_OpenSimPrim *>(prim_component.get());
	        
		//m->AddU32(prim->LocalId);
		std::string retstr = "local id:" + prim->FullId.ToString() + "- prim name: " + prim->ObjectName;
		return PyString_FromString(retstr.c_str());
	}

	else if (s_name.compare("place") == 0)
	{
        const Foundation::ComponentInterfacePtr &ogre_component = entity->GetComponent("EC_OgrePlaceable");
		if (!ogre_component)
            return NULL; //XXX report AttributeError        
		OgreRenderer::EC_OgrePlaceable *placeable = checked_static_cast<OgreRenderer::EC_OgrePlaceable *>(ogre_component.get());
		
		/* this must probably return a new object, a 'Place' instance, that has these.
		   or do we wanna hide the E-C system in the api and have these directly on entity? 
		   probably not a good idea to hide the actual system that much. or? */
		RexTypes::Vector3 pos = placeable->GetPosition();
		//RexTypes::Vector3 scale = ogre_pos->GetScale();
		//RexTypes::Vector3 rot = Core::PackQuaternionToFloat3(ogre_pos->GetOrientation());
		/* .. i guess best to wrap the Rex Vector and other types soon,
		   the pyrr irrlicht binding project does it for these using swig,
		   https://opensvn.csie.org/traccgi/pyrr/browser/pyrr/irrlicht.i */
		return Py_BuildValue("fff", pos.x, pos.y, pos.z);
	}

	std::cout << "unknown component type."  << std::endl;
	return NULL;
}

int PythonScript::entity_setattro(PyObject *self, PyObject *name, PyObject *value)
{
	/*
	if (!(tmp = PyObject_GenericSetAttr((PyObject*)self, name, value))) {
		if (!PyErr_ExceptionMatches(PyExc_AttributeError))
			return NULL;
		PyErr_Clear();
	}*/

	const char* c_name = PyString_AsString(name);
	std::string s_name = std::string(c_name);

	std::cout << "Entity: setting unknown attribute: " << s_name;
	rexviewer_EntityObject *eob = (rexviewer_EntityObject *)self;

	//entity_ptrs map usage
	/* this crashes now in boost, 
	   void add_ref_copy() { BOOST_INTERLOCKED_INCREMENT( &use_count_ );
	std::map<Core::entity_id_t, Scene::EntityPtr>::iterator ep_iter = entity_ptrs.find(self->ent_id);
	Scene::EntityPtr entity = ep_iter->second;
	fix.. */

	/* re-getting the EntityPtr as it wasn't stored anywhere yet,
	   is copy-paste from PythonScriptModule GetEntity 
	   but to be removed when that map is used above.*/
	Scene::ScenePtr scene = PythonScript::GetScene();
	Scene::EntityPtr entity = scene->GetEntity(eob->ent_id);
	
	/*if (s_name.compare("prim") == 0)
	{
		std::cout << ".. getting prim" << std::endl;
		const Foundation::ComponentInterfacePtr &prim_component = entity->GetComponent("EC_OpenSimPrim");
		if (!prim_component)
			return NULL; //XXX report AttributeError
		RexLogic::EC_OpenSimPrim *prim = checked_static_cast<RexLogic::EC_OpenSimPrim *>(prim_component.get());
	        
		//m->AddU32(prim->LocalId);
		std::string retstr = "local id:" + prim->FullId.ToString() + "- prim name: " + prim->ObjectName;
		return PyString_FromString(retstr.c_str());
	}*/

	//else 
	if (s_name.compare("place") == 0)
	{
        const Foundation::ComponentInterfacePtr &ogre_component = entity->GetComponent("EC_OgrePlaceable");
		if (!ogre_component)
            return NULL; //XXX report AttributeError        
		OgreRenderer::EC_OgrePlaceable *placeable = checked_static_cast<OgreRenderer::EC_OgrePlaceable *>(ogre_component.get());
		
		/* this must probably return a new object, a 'Place' instance, that has these.
		   or do we wanna hide the E-C system in the api and have these directly on entity? 
		   probably not a good idea to hide the actual system that much. or? */
		float x, y, z;
		if(!PyArg_ParseTuple(value, "fff", &x, &y, &z))
			return NULL; //XXX report ArgumentException error
		placeable->SetPosition(Vector3(x, y, z));
		//RexTypes::Vector3 scale = ogre_pos->GetScale();
		//RexTypes::Vector3 rot = Core::PackQuaternionToFloat3(ogre_pos->GetOrientation());
		/* .. i guess best to wrap the Rex Vector and other types soon,
		   the pyrr irrlicht binding project does it for these using swig,
		   https://opensvn.csie.org/traccgi/pyrr/browser/pyrr/irrlicht.i */

		/* sending a scene updated event to trigger network synch,
		   copy-paste from DebugStats, 
		   perhaps there'll be some MoveEntity thing in logic that can reuse for this? */
	    Scene::Events::SceneEventData event_data(eob->ent_id);
		event_data.entity_ptr_list.push_back(entity);
		PythonScript::staticframework->GetEventManager()->SendEvent(PythonScript::scene_event_category_, Scene::Events::EVENT_ENTITY_UPDATED, &event_data);

		return 0; //success.
	}

	std::cout << "unknown component type."  << std::endl;
	return -1; //the way for setattr to report a failure
}

