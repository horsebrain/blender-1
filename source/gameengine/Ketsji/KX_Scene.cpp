/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 * Ketsji scene. Holds references to all scene data.
 */

/** \file gameengine/Ketsji/KX_Scene.cpp
 *  \ingroup ketsji
 */


#ifdef _MSC_VER
#  pragma warning (disable:4786)
#endif

#include "KX_Scene.h"
#include "KX_Globals.h"
#include "BLI_utildefines.h"
#include "KX_KetsjiEngine.h"
#include "KX_BlenderMaterial.h"
#include "KX_TextMaterial.h"
#include "KX_FontObject.h"
#include "RAS_IPolygonMaterial.h"
#include "EXP_ListValue.h"
#include "SCA_LogicManager.h"
#include "SCA_TimeEventManager.h"
#include "SCA_2DFilterActuator.h"
#include "SCA_PythonController.h"
#include "KX_CollisionEventManager.h"
#include "SCA_KeyboardManager.h"
#include "SCA_MouseManager.h"
#include "SCA_ActuatorEventManager.h"
#include "SCA_BasicEventManager.h"
#include "KX_Camera.h"
#include "SCA_JoystickManager.h"
#include "KX_PyMath.h"
#include "RAS_MeshObject.h"
#include "SCA_IScene.h"
#include "KX_LodManager.h"
#include "KX_CullingHandler.h"

#include "RAS_Rasterizer.h"
#include "RAS_ICanvas.h"
#include "RAS_2DFilterData.h"
#include "KX_2DFilterManager.h"
#include "RAS_BoundingBoxManager.h"
#include "RAS_BucketManager.h"

#include "EXP_FloatValue.h"
#include "SCA_IController.h"
#include "SCA_IActuator.h"
#include "SG_Node.h"
#include "SG_Controller.h"
#include "SG_Node.h"
#include "DNA_group_types.h"
#include "DNA_scene_types.h"
#include "DNA_property_types.h"

#include "KX_NodeRelationships.h"

#include "KX_NetworkMessageScene.h"
#include "PHY_IPhysicsEnvironment.h"
#include "PHY_IGraphicController.h"
#include "PHY_IPhysicsController.h"
#include "BL_BlenderConverter.h"
#include "KX_MotionState.h"

#include "BL_ModifierDeformer.h"
#include "BL_ShapeDeformer.h"
#include "BL_DeformableGameObject.h"
#include "KX_ObstacleSimulation.h"

#ifdef WITH_BULLET
#  include "KX_SoftBodyDeformer.h"
#endif

#ifdef WITH_PYTHON
#  include "EXP_PythonCallBack.h"
#endif

#include "KX_Light.h"

#include "BLI_task.h"

#include "CM_Message.h"
#include "CM_List.h"

static void *KX_SceneReplicationFunc(SG_Node* node,void* gameobj,void* scene)
{
	KX_GameObject* replica = ((KX_Scene*)scene)->AddNodeReplicaObject(node,(KX_GameObject*)gameobj);

	if (replica)
		replica->Release();

	return (void*)replica;
}

static void *KX_SceneDestructionFunc(SG_Node* node,void* gameobj,void* scene)
{
	((KX_Scene*)scene)->RemoveNodeDestructObject(node,(KX_GameObject*)gameobj);

	return nullptr;
};

bool KX_Scene::KX_ScenegraphUpdateFunc(SG_Node* node,void* gameobj,void* scene)
{
	return node->Schedule(((KX_Scene*)scene)->m_sghead);
}

bool KX_Scene::KX_ScenegraphRescheduleFunc(SG_Node* node,void* gameobj,void* scene)
{
	return node->Reschedule(((KX_Scene*)scene)->m_sghead);
}

SG_Callbacks KX_Scene::m_callbacks = SG_Callbacks(
	KX_SceneReplicationFunc,
	KX_SceneDestructionFunc,
	KX_GameObject::UpdateTransformFunc,
	KX_Scene::KX_ScenegraphUpdateFunc,
	KX_Scene::KX_ScenegraphRescheduleFunc);

KX_Scene::KX_Scene(SCA_IInputDevice *inputDevice,
				   const std::string& sceneName,
				   Scene *scene,
				   class RAS_ICanvas* canvas,
				   KX_NetworkMessageManager *messageManager): 
	EXP_Value(),
	m_keyboardmgr(nullptr),
	m_mousemgr(nullptr),
	m_physicsEnvironment(0),
	m_sceneName(sceneName),
	m_active_camera(nullptr),
	m_overrideCullingCamera(nullptr),
	m_ueberExecutionPriority(0),
	m_suspendeddelta(0.0),
	m_activityCulling(false),
	m_blenderScene(scene),
	m_isActivedHysteresis(false),
	m_lodHysteresisValue(0)
{

	m_dbvt_culling = false;
	m_dbvt_occlusion_res = 0;
	m_suspend = false;
	m_objectlist = new EXP_ListValue<KX_GameObject>();
	m_parentlist = new EXP_ListValue<KX_GameObject>();
	m_lightlist = new EXP_ListValue<KX_LightObject>();
	m_inactivelist = new EXP_ListValue<KX_GameObject>();
	m_cameralist = new EXP_ListValue<KX_Camera>();
	m_fontlist = new EXP_ListValue<KX_FontObject>();

	m_filterManager = new KX_2DFilterManager();
	m_logicmgr = new SCA_LogicManager();
	
	m_timemgr = new SCA_TimeEventManager(m_logicmgr);
	m_keyboardmgr = new SCA_KeyboardManager(m_logicmgr, inputDevice);
	m_mousemgr = new SCA_MouseManager(m_logicmgr,inputDevice);
	
	SCA_ActuatorEventManager* actmgr = new SCA_ActuatorEventManager(m_logicmgr);
	SCA_BasicEventManager* basicmgr = new SCA_BasicEventManager(m_logicmgr);

	m_logicmgr->RegisterEventManager(actmgr);
	m_logicmgr->RegisterEventManager(m_keyboardmgr);
	m_logicmgr->RegisterEventManager(m_mousemgr);
	m_logicmgr->RegisterEventManager(m_timemgr);
	m_logicmgr->RegisterEventManager(basicmgr);

	SCA_JoystickManager *joymgr = new SCA_JoystickManager(m_logicmgr);
	m_logicmgr->RegisterEventManager(joymgr);

	m_networkScene = new KX_NetworkMessageScene(messageManager);
	
	m_rootnode = nullptr;

	m_rendererManager = new KX_TextureRendererManager(this);
	KX_TextMaterial *textMaterial = new KX_TextMaterial();
	m_bucketmanager=new RAS_BucketManager(textMaterial);
	m_boundingBoxManager = new RAS_BoundingBoxManager();

	m_animationPool = BLI_task_pool_create(KX_GetActiveEngine()->GetTaskScheduler(), &m_animationPoolData);

#ifdef WITH_PYTHON
	m_attr_dict = nullptr;

	for (unsigned short i = 0; i < MAX_DRAW_CALLBACK; ++i) {
		m_drawCallbacks[i] = nullptr;
	}
#endif
}



KX_Scene::~KX_Scene()
{
	// The release of debug properties used to be in SCA_IScene::~SCA_IScene
	// It's still there but we remove all properties here otherwise some
	// reference might be hanging and causing late release of objects
	RemoveAllDebugProperties();

	while (GetRootParentList()->GetCount() > 0) 
	{
		KX_GameObject* parentobj = GetRootParentList()->GetValue(0);
		this->RemoveObject(parentobj);
	}

	if (m_obstacleSimulation)
		delete m_obstacleSimulation;

	if (m_animationPool) {
		BLI_task_pool_free(m_animationPool);
	}

	if (m_objectlist)
		m_objectlist->Release();

	if (m_parentlist)
		m_parentlist->Release();
	
	if (m_inactivelist)
		m_inactivelist->Release();

	if (m_lightlist)
		m_lightlist->Release();

	if (m_cameralist) {
		m_cameralist->Release();
	}

	if (m_fontlist) {
		m_fontlist->Release();
	}

	if (m_filterManager) {
		delete m_filterManager;
	}

	if (m_logicmgr)
		delete m_logicmgr;

	if (m_physicsEnvironment)
		delete m_physicsEnvironment;

	if (m_networkScene)
		delete m_networkScene;

	if (m_rendererManager) {
		delete m_rendererManager;
	}

	if (m_bucketmanager)
	{
		delete m_bucketmanager;
	}

	if (m_boundingBoxManager) {
		delete m_boundingBoxManager;
	}

#ifdef WITH_PYTHON
	if (m_attr_dict) {
		PyDict_Clear(m_attr_dict);
		/* Py_CLEAR: Py_DECREF's and nullptr's */
		Py_CLEAR(m_attr_dict);
	}

	/* these may be nullptr but the macro checks */
	for (unsigned short i = 0; i < MAX_DRAW_CALLBACK; ++i) {
		Py_CLEAR(m_drawCallbacks[i]);
	}
#endif
}

std::string KX_Scene::GetName()
{
	return m_sceneName;
}

/// Set the name of the value
void KX_Scene::SetName(const std::string& name)
{
	m_sceneName = name;
}

RAS_BucketManager* KX_Scene::GetBucketManager() const
{
	return m_bucketmanager;
}

KX_TextureRendererManager *KX_Scene::GetTextureRendererManager() const
{
	return m_rendererManager;
}

RAS_BoundingBoxManager *KX_Scene::GetBoundingBoxManager() const
{
	return m_boundingBoxManager;
}

EXP_ListValue<KX_GameObject> *KX_Scene::GetObjectList() const
{
	return m_objectlist;
}

EXP_ListValue<KX_GameObject> *KX_Scene::GetRootParentList() const
{
	return m_parentlist;
}

EXP_ListValue<KX_GameObject> *KX_Scene::GetInactiveList() const
{
	return m_inactivelist;
}

EXP_ListValue<KX_LightObject> *KX_Scene::GetLightList() const
{
	return m_lightlist;
}

SCA_LogicManager* KX_Scene::GetLogicManager() const
{
	return m_logicmgr;
}

SCA_TimeEventManager* KX_Scene::GetTimeEventManager() const
{
	return m_timemgr;
}

KX_PythonComponentManager& KX_Scene::GetPythonComponentManager()
{
	return m_componentManager;
}

EXP_ListValue<KX_Camera> *KX_Scene::GetCameraList() const
{
	return m_cameralist;
}

EXP_ListValue<KX_FontObject> *KX_Scene::GetFontList() const
{
	return m_fontlist;
}

void KX_Scene::SetFramingType(RAS_FrameSettings & frame_settings)
{
	m_frame_settings = frame_settings;
};

/**
 * Return a const reference to the framing 
 * type set by the above call.
 * The contents are not guaranteed to be sensible
 * if you don't call the above function.
 */
const RAS_FrameSettings& KX_Scene::GetFramingType() const 
{
	return m_frame_settings;
}

void KX_Scene::SetWorldInfo(class KX_WorldInfo* worldinfo)
{
	m_worldinfo = worldinfo;
}



class KX_WorldInfo* KX_Scene::GetWorldInfo()
{
	return m_worldinfo;
}

void KX_Scene::Suspend()
{
	m_suspend = true;
}

void KX_Scene::Resume()
{
	m_suspend = false;
}

void KX_Scene::SetActivityCulling(bool b)
{
	m_activityCulling = b;
}

bool KX_Scene::IsSuspended()
{
	return m_suspend;
}

void KX_Scene::AddObjectDebugProperties(class KX_GameObject* gameobj)
{
	Object* blenderobject = gameobj->GetBlenderObject();
	if (!blenderobject) {
		return;
	}

	bProperty* prop = (bProperty*)blenderobject->prop.first;

	while (prop) {
		if (prop->flag & PROP_DEBUG)
			AddDebugProperty(gameobj, prop->name);
		prop = prop->next;
	}	

	if (blenderobject->scaflag & OB_DEBUGSTATE)
		AddDebugProperty(gameobj, "__state__");
}

void KX_Scene::RemoveNodeDestructObject(SG_Node* node, KX_GameObject *gameobj)
{
	if (NewRemoveObject(gameobj)) {
		// object is not yet deleted because a reference is hanging somewhere.
		// This should not happen anymore since we use proxy object for Python.
		CM_Error("zombie object! name=" << gameobj->GetName());
		BLI_assert(false);
	}
	if (node)
		delete node;
}

KX_GameObject* KX_Scene::AddNodeReplicaObject(SG_Node* node, KX_GameObject *gameobj)
{
	// for group duplication, limit the duplication of the hierarchy to the
	// objects that are part of the group. 
	if (!IsObjectInGroup(gameobj))
		return nullptr;
	
	KX_GameObject* newobj = (KX_GameObject*)gameobj->GetReplica();
	m_map_gameobject_to_replica[gameobj] = newobj;

	// also register 'timers' (time properties) of the replica
	int numprops = newobj->GetPropertyCount();

	for (int i = 0; i < numprops; i++)
	{
		EXP_Value* prop = newobj->GetProperty(i);

		if (prop->GetProperty("timer"))
			this->m_timemgr->AddTimeProperty(prop);
	}

	if (node)
	{
		newobj->SetSGNode(node);
	}
	else
	{
		m_rootnode = new SG_Node(newobj,this,KX_Scene::m_callbacks);
	
		// this fixes part of the scaling-added object bug
		SG_Node* orgnode = gameobj->GetSGNode();
		m_rootnode->SetLocalScale(orgnode->GetLocalScale());
		m_rootnode->SetLocalPosition(orgnode->GetLocalPosition());
		m_rootnode->SetLocalOrientation(orgnode->GetLocalOrientation());

		// define the relationship between this node and it's parent.
		KX_NormalParentRelation *parent_relation = new KX_NormalParentRelation();
		m_rootnode->SetParentRelation(parent_relation);

		newobj->SetSGNode(m_rootnode);
	}
	
	SG_Node* replicanode = newobj->GetSGNode();
//	SG_Node* rootnode = (replicanode == m_rootnode ? nullptr : m_rootnode);

	// Add the object in the obstacle simulation if needed.
	if (m_obstacleSimulation && gameobj->GetBlenderObject()->gameflag & OB_HASOBSTACLE) {
		m_obstacleSimulation->AddObstacleForObj(newobj);
	}

	// Register object for component update.
	if (gameobj->GetComponents()) {
		m_componentManager.RegisterObject(newobj);
	}

	replicanode->SetSGClientObject(newobj);

	// this is the list of object that are send to the graphics pipeline
	m_objectlist->Add(CM_AddRef(newobj));
	switch (newobj->GetGameObjectType()) {
		case SCA_IObject::OBJ_LIGHT:
		{
			m_lightlist->Add(CM_AddRef(static_cast<KX_LightObject *>(newobj)));
			break;
		}
		case SCA_IObject::OBJ_TEXT:
		{
			m_fontlist->Add(CM_AddRef(static_cast<KX_FontObject *>(newobj)));
			break;
		}
		case SCA_IObject::OBJ_CAMERA:
		{
			m_cameralist->Add(CM_AddRef(static_cast<KX_Camera *>(newobj)));
			break;
		}
		case SCA_IObject::OBJ_ARMATURE:
		{
			AddAnimatedObject(newobj);
			break;
		}
	}
	newobj->AddMeshUser();

	// logic cannot be replicated, until the whole hierarchy is replicated.
	m_logicHierarchicalGameObjects.push_back(newobj);
	//replicate controllers of this node
	SGControllerList	scenegraphcontrollers = gameobj->GetSGNode()->GetSGControllerList();
	replicanode->RemoveAllControllers();

	for (SG_Controller *controller : scenegraphcontrollers) {
		// controller replication is quite complicated
		// only replicate ipo controller for now

		SG_Controller* replicacontroller = controller->GetReplica(replicanode);
		if (replicacontroller)
		{
			replicacontroller->SetNode(replicanode);
			replicanode->AddSGController(replicacontroller);
		}
	}
	// replicate graphic controller
	if (gameobj->GetGraphicController())
	{
		PHY_IMotionState* motionstate = new KX_MotionState(newobj->GetSGNode());
		PHY_IGraphicController* newctrl = gameobj->GetGraphicController()->GetReplica(motionstate);
		newctrl->SetNewClientInfo(newobj->getClientInfo());
		newobj->SetGraphicController(newctrl);
	}

	// replicate physics controller
	if (gameobj->GetPhysicsController())
	{
		PHY_IMotionState* motionstate = new KX_MotionState(newobj->GetSGNode());
		PHY_IPhysicsController* newctrl = gameobj->GetPhysicsController()->GetReplica();

		KX_GameObject *parent = newobj->GetParent();
		PHY_IPhysicsController* parentctrl = (parent) ? parent->GetPhysicsController() : nullptr;

		newctrl->SetNewClientInfo(newobj->getClientInfo());
		newobj->SetPhysicsController(newctrl);
		newctrl->PostProcessReplica(motionstate, parentctrl);

		// Child objects must be static
		if (parent)
			newctrl->SuspendDynamics();
	}

	// Always make sure that the bounding box is valid.
	newobj->UpdateBounds(true);

	return newobj;
}



// before calling this method KX_Scene::ReplicateLogic(), make sure to
// have called 'GameObject::ReParentLogic' for each object this
// hierarchy that's because first ALL bricks must exist in the new
// replica of the hierarchy in order to make cross-links work properly
// !
// It is VERY important that the order of sensors and actuators in
// the replicated object is preserved: it is used to reconnect the logic.
// This method is more robust then using the bricks name in case of complex 
// group replication. The replication of logic bricks is done in 
// SCA_IObject::ReParentLogic(), make sure it preserves the order of the bricks.
void KX_Scene::ReplicateLogic(KX_GameObject* newobj)
{
	/* add properties to debug list, for added objects and DupliGroups */
	if (KX_GetActiveEngine()->GetFlag(KX_KetsjiEngine::AUTO_ADD_DEBUG_PROPERTIES)) {
		AddObjectDebugProperties(newobj);
	}
	// also relink the controller to sensors/actuators
	const SCA_ControllerList controllers = newobj->GetControllers();
	//SCA_SensorList&     sensors     = newobj->GetSensors();
	//SCA_ActuatorList&   actuators   = newobj->GetActuators();

	for (SCA_IController *cont : controllers) {
		cont->SetUeberExecutePriority(m_ueberExecutionPriority);
		const SCA_SensorList linkedsensors = cont->GetLinkedSensors();
		const SCA_ActuatorList linkedactuators = cont->GetLinkedActuators();

		// disconnect the sensors and actuators
		// do it directly on the list at this controller is not connected to anything at this stage
		cont->GetLinkedSensors().clear();
		cont->GetLinkedActuators().clear();
		
		// now relink each sensor
		for (SCA_ISensor *oldsensor : linkedsensors) {
			SCA_IObject* oldsensorobj = oldsensor->GetParent();
			// the original owner of the sensor has been replicated?
			SCA_IObject* newsensorobj = m_map_gameobject_to_replica[oldsensorobj];

			if (!newsensorobj)
			{
				// no, then the sensor points outside the hierarchy, keep it the same
				if (m_objectlist->SearchValue(static_cast<KX_GameObject *>(oldsensorobj)))
					// only replicate links that points to active objects
					m_logicmgr->RegisterToSensor(cont,oldsensor);
			}
			else
			{
				// yes, then the new sensor has the same position
				SCA_SensorList& sensorlist = oldsensorobj->GetSensors();
				SCA_SensorList::iterator sit;
				SCA_ISensor* newsensor = nullptr;
				int sensorpos;

				for (sensorpos=0, sit=sensorlist.begin(); sit!=sensorlist.end(); sit++, sensorpos++)
				{
					if ((*sit) == oldsensor) 
					{
						newsensor = newsensorobj->GetSensors().at(sensorpos);
						break;
					}
				}
				BLI_assert(newsensor != nullptr);
				m_logicmgr->RegisterToSensor(cont,newsensor);
			}
		}
		
		// now relink each actuator
		for (SCA_IActuator *oldactuator : linkedactuators) {
			SCA_IObject* oldactuatorobj = oldactuator->GetParent();
			SCA_IObject* newactuatorobj = m_map_gameobject_to_replica[oldactuatorobj];

			if (!newactuatorobj)
			{
				// no, then the sensor points outside the hierarchy, keep it the same
				if (m_objectlist->SearchValue(static_cast<KX_GameObject *>(oldactuatorobj)))
					// only replicate links that points to active objects
					m_logicmgr->RegisterToActuator(cont,oldactuator);
			}
			else
			{
				// yes, then the new sensor has the same position
				SCA_ActuatorList& actuatorlist = oldactuatorobj->GetActuators();
				SCA_ActuatorList::iterator ait;
				SCA_IActuator* newactuator = nullptr;
				int actuatorpos;

				for (actuatorpos=0, ait=actuatorlist.begin(); ait!=actuatorlist.end(); ait++, actuatorpos++)
				{
					if ((*ait) == oldactuator) 
					{
						newactuator = newactuatorobj->GetActuators().at(actuatorpos);
						break;
					}
				}
				BLI_assert(newactuator != nullptr);
				m_logicmgr->RegisterToActuator(cont,newactuator);
				newactuator->SetUeberExecutePriority(m_ueberExecutionPriority);
			}
		}
	}
	// ready to set initial state
	newobj->ResetState();
}

void KX_Scene::DupliGroupRecurse(KX_GameObject *groupobj, int level)
{
	Object* blgroupobj = groupobj->GetBlenderObject();
	std::vector<KX_GameObject*> duplilist;

	if (!groupobj->GetSGNode() ||
		!groupobj->IsDupliGroup() ||
		level>MAX_DUPLI_RECUR)
		return;

	// we will add one group at a time
	m_logicHierarchicalGameObjects.clear();
	m_map_gameobject_to_replica.clear();
	m_ueberExecutionPriority++;
	// for groups will do something special: 
	// we will force the creation of objects to those in the group only
	// Again, this is match what Blender is doing (it doesn't care of parent relationship)
	m_groupGameObjects.clear();

	Group *group = blgroupobj->dup_group;
	for (GroupObject *go=(GroupObject*)group->gobject.first; go; go=(GroupObject*)go->next) 
	{
		Object* blenderobj = go->ob;
		if (blgroupobj == blenderobj)
			// this check is also in group_duplilist()
			continue;

		KX_GameObject *gameobj = (KX_GameObject*)m_logicmgr->FindGameObjByBlendObj(blenderobj);
		if (gameobj == nullptr) 
		{
			// this object has not been converted!!!
			// Should not happen as dupli group are created automatically 
			continue;
		}

		gameobj->SetBlenderGroupObject(blgroupobj);

		if ((blenderobj->lay & group->layer)==0)
		{
			// object is not visible in the 3D view, will not be instantiated
			continue;
		}
		m_groupGameObjects.insert(gameobj);
	}

	for (KX_GameObject *gameobj : m_groupGameObjects) {
		KX_GameObject *parent = gameobj->GetParent();
		if (parent != nullptr)
		{
			// this object is not a top parent. Either it is the child of another
			// object in the group and it will be added automatically when the parent
			// is added. Or it is the child of an object outside the group and the group
			// is inconsistent, skip it anyway
			continue;
		}
		KX_GameObject *replica = (KX_GameObject*) AddNodeReplicaObject(nullptr,gameobj);
		// add to 'rootparent' list (this is the list of top hierarchy objects, updated each frame)
		m_parentlist->Add(CM_AddRef(replica));

		// recurse replication into children nodes
		const NodeList children = gameobj->GetSGNode()->GetSGChildren();

		replica->GetSGNode()->ClearSGChildren();
		for (SG_Node *orgnode : children) {
			SG_Node* childreplicanode = orgnode->GetSGReplica();
			if (childreplicanode)
				replica->GetSGNode()->AddChild(childreplicanode);
		}
		// don't replicate logic now: we assume that the objects in the group can have
		// logic relationship, even outside parent relationship
		// In order to match 3D view, the position of groupobj is used as a 
		// transformation matrix instead of the new position. This means that 
		// the group reference point is 0,0,0

		// get the rootnode's scale
		MT_Vector3 newscale = groupobj->NodeGetWorldScaling();
		// set the replica's relative scale with the rootnode's scale
		replica->NodeSetRelativeScale(newscale);

		MT_Vector3 offset(group->dupli_ofs);
		MT_Vector3 newpos = groupobj->NodeGetWorldPosition() + 
			newscale*(groupobj->NodeGetWorldOrientation() * (gameobj->NodeGetWorldPosition()-offset));
		replica->NodeSetLocalPosition(newpos);
		// set the orientation after position for softbody!
		MT_Matrix3x3 newori = groupobj->NodeGetWorldOrientation() * gameobj->NodeGetWorldOrientation();
		replica->NodeSetLocalOrientation(newori);
		// update scenegraph for entire tree of children
		replica->GetSGNode()->UpdateWorldData(0);
		// we can now add the graphic controller to the physic engine
		replica->ActivateGraphicController(true);

		// done with replica
		replica->Release();
	}

	// the logic must be replicated first because we need
	// the new logic bricks before relinking
	for (KX_GameObject *gameobj : m_logicHierarchicalGameObjects) {
		gameobj->ReParentLogic();
	}
	
	//	relink any pointers as necessary, sort of a temporary solution
	for (KX_GameObject *gameobj : m_logicHierarchicalGameObjects) {
		// this will also relink the actuator to objects within the hierarchy
		gameobj->Relink(m_map_gameobject_to_replica);
		// add the object in the layer of the parent
		gameobj->SetLayer(groupobj->GetLayer());
	}

	// replicate crosslinks etc. between logic bricks
	for (KX_GameObject *gameobj : m_logicHierarchicalGameObjects) {
		ReplicateLogic(gameobj);
	}
	
	// now look if object in the hierarchy have dupli group and recurse
	for (KX_GameObject *gameobj : m_logicHierarchicalGameObjects) {
		/* Replicate all constraints. */
		if (gameobj->GetPhysicsController()) {
			gameobj->GetPhysicsController()->ReplicateConstraints(gameobj, m_logicHierarchicalGameObjects);
			gameobj->ClearConstraints();
		}

		if (gameobj != groupobj && gameobj->IsDupliGroup())
			// can't instantiate group immediately as it destroys m_logicHierarchicalGameObjects
			duplilist.push_back(gameobj);

		if (gameobj->GetBlenderGroupObject() == blgroupobj) {
			// set references for dupli-group
			// groupobj holds a list of all objects, that belongs to this group
			groupobj->AddInstanceObjects(gameobj);

			// every object gets the reference to its dupli-group object
			gameobj->SetDupliGroupObject(groupobj);
		}
	}

	for (KX_GameObject *gameobj : duplilist) {
		DupliGroupRecurse(gameobj, level+1);
	}
}


KX_GameObject *KX_Scene::AddReplicaObject(KX_GameObject *originalobject, KX_GameObject *referenceobject, float lifespan)
{
	m_logicHierarchicalGameObjects.clear();
	m_map_gameobject_to_replica.clear();
	m_groupGameObjects.clear();

	KX_GameObject* originalobj = (KX_GameObject*) originalobject;
	KX_GameObject* referenceobj = (KX_GameObject*) referenceobject;

	m_ueberExecutionPriority++;

	// lets create a replica
	KX_GameObject* replica = (KX_GameObject*) AddNodeReplicaObject(nullptr,originalobj);

	// add a timebomb to this object
	// lifespan of zero means 'this object lives forever'
	if (lifespan > 0.0f)
	{
		// for now, convert between so called frames and realtime
		m_tempObjectList.push_back(replica);
		// this convert the life from frames to sort-of seconds, hard coded 0.02 that assumes we have 50 frames per second
		// if you change this value, make sure you change it in KX_GameObject::pyattr_get_life property too
		EXP_Value *fval = new EXP_FloatValue(lifespan*0.02f);
		replica->SetProperty("::timebomb",fval);
		fval->Release();
	}

	// add to 'rootparent' list (this is the list of top hierarchy objects, updated each frame)
	m_parentlist->Add(CM_AddRef(replica));

	// recurse replication into children nodes

	const NodeList children = originalobj->GetSGNode()->GetSGChildren();

	replica->GetSGNode()->ClearSGChildren();
	for (SG_Node *orgnode : children) {
		SG_Node* childreplicanode = orgnode->GetSGReplica();
		if (childreplicanode)
			replica->GetSGNode()->AddChild(childreplicanode);
	}

	if (referenceobj) {
		// At this stage all the objects in the hierarchy have been duplicated,
		// we can update the scenegraph, we need it for the duplication of logic
		MT_Vector3 newpos = referenceobj->NodeGetWorldPosition();
		replica->NodeSetLocalPosition(newpos);

		MT_Matrix3x3 newori = referenceobj->NodeGetWorldOrientation();
		replica->NodeSetLocalOrientation(newori);

		// get the rootnode's scale
		MT_Vector3 newscale = referenceobj->GetSGNode()->GetRootSGParent()->GetLocalScale();
		// set the replica's relative scale with the rootnode's scale
		replica->NodeSetRelativeScale(newscale);
	}

	replica->GetSGNode()->UpdateWorldData(0);
	// the size is correct, we can add the graphic controller to the physic engine
	replica->ActivateGraphicController(true);

	// now replicate logic
	for (KX_GameObject *gameobj : m_logicHierarchicalGameObjects) {
		gameobj->ReParentLogic();
	}
	
	//	relink any pointers as necessary, sort of a temporary solution
	for (KX_GameObject *gameobj : m_logicHierarchicalGameObjects) {
		// this will also relink the actuators in the hierarchy
		gameobj->Relink(m_map_gameobject_to_replica);
		if (referenceobj) {
			// add the object in the layer of the reference object
			gameobj->SetLayer(referenceobj->GetLayer());
		}
		else {
			// We don't know what layer set, so we set all visible layers in the blender scene.
			gameobj->SetLayer(m_blenderScene->lay);
		}
	}

	// replicate crosslinks etc. between logic bricks
	for (KX_GameObject *gameobj : m_logicHierarchicalGameObjects) {
		ReplicateLogic(gameobj);
	}
	
	// check if there are objects with dupligroup in the hierarchy
	std::vector<KX_GameObject*> duplilist;
	for (KX_GameObject *gameobj : m_logicHierarchicalGameObjects) {
		if (gameobj->IsDupliGroup())
		{
			// separate list as m_logicHierarchicalGameObjects is also used by DupliGroupRecurse()
			duplilist.push_back(gameobj);
		}
	}
	for (KX_GameObject *gameobj : duplilist) {
		DupliGroupRecurse(gameobj, 0);
	}

	//	don't release replica here because we are returning it, not done with it...
	return replica;
}



void KX_Scene::RemoveObject(KX_GameObject *gameobj)
{
	// disconnect child from parent
	SG_Node* node = gameobj->GetSGNode();

	if (node)
	{
		node->DisconnectFromParent();

		// recursively destruct
		node->Destruct();
	}
}

void KX_Scene::RemoveDupliGroup(KX_GameObject *gameobj)
{
	if (gameobj->IsDupliGroup()) {
		for (KX_GameObject *instance : gameobj->GetInstanceObjects()) {
			DelayedRemoveObject(instance);
		}
	}
}

void KX_Scene::DelayedRemoveObject(KX_GameObject *gameobj)
{
	RemoveDupliGroup(gameobj);

	CM_ListAddIfNotFound(m_euthanasyobjects, gameobj);
}

bool KX_Scene::NewRemoveObject(KX_GameObject *gameobj)
{
	/* remove property from debug list */
	RemoveObjectDebugProperties(gameobj);

	/* Invalidate the python reference, since the object may exist in script lists
	 * its possible that it wont be automatically invalidated, so do it manually here,
	 * 
	 * if for some reason the object is added back into the scene python can always get a new Proxy
	 */
	gameobj->InvalidateProxy();

	// keep the blender->game object association up to date
	// note that all the replicas of an object will have the same
	// blender object, that's why we need to check the game object
	// as only the deletion of the original object must be recorded
	if (gameobj->GetBlenderObject()) {
		// In some case the game object can contains a nullptr blender object e.g default camera.
		m_logicmgr->UnregisterGameObj(gameobj->GetBlenderObject(), gameobj);
	}

	// remove all sensors/controllers/actuators from logicsystem...
	
	SCA_SensorList& sensors = gameobj->GetSensors();
	for (SCA_ISensor *sensor : sensors) {
		m_logicmgr->RemoveSensor(sensor);
	}

	SCA_ControllerList& controllers = gameobj->GetControllers();
	for (SCA_IController *controller : controllers) {
		m_logicmgr->RemoveController(controller);
		controller->ReParent(nullptr);
	}

	SCA_ActuatorList& actuators = gameobj->GetActuators();
	for (SCA_IActuator *actuator : actuators) {
		m_logicmgr->RemoveActuator(actuator);
	}
	// the sensors/controllers/actuators must also be released, this is done in ~SCA_IObject

	// now remove the timer properties from the time manager
	int numprops = gameobj->GetPropertyCount();

	for (int i = 0; i < numprops; i++)
	{
		EXP_Value* propval = gameobj->GetProperty(i);
		if (propval->GetProperty("timer"))
		{
			m_timemgr->RemoveTimeProperty(propval);
		}
	}

	// if the object is the dupligroup proxy, you have to cleanup all m_pDupliGroupObject's in all
	// instances refering to this group
	if (gameobj->GetInstanceObjects()) {
		for (KX_GameObject *instance : gameobj->GetInstanceObjects()) {
			instance->RemoveDupliGroupObject();
		}
	}

	// if this object was part of a group, make sure to remove it from that group's instance list
	KX_GameObject *group = gameobj->GetDupliGroupObject();
	if (group)
		group->RemoveInstanceObject(gameobj);

	if (m_obstacleSimulation) {
		m_obstacleSimulation->DestroyObstacleForObj(gameobj);
	}

	m_componentManager.UnregisterObject(gameobj);

	gameobj->RemoveMeshes();

	m_rendererManager->InvalidateViewpoint(gameobj);

	bool ret = true;
	if (m_lightlist->RemoveValue(gameobj)) {
		ret = (gameobj->Release() != nullptr);
	}
	if (m_objectlist->RemoveValue(gameobj)) {
		ret = (gameobj->Release() != nullptr);
	}
	if (m_parentlist->RemoveValue(gameobj)) {
		ret = (gameobj->Release() != nullptr);
	}
	if (m_inactivelist->RemoveValue(gameobj)) {
		ret = (gameobj->Release() != nullptr);
	}
	if (m_fontlist->RemoveValue(gameobj)) {
		ret = (gameobj->Release() != nullptr);
	}
	if (m_cameralist->RemoveValue(gameobj)) {
		ret = (gameobj->Release() != nullptr);
	}

	// WARNING: 'gameobj' maybe be freed now, only compare, don't access.
	CM_ListRemoveIfFound(m_animatedlist, gameobj);
	CM_ListRemoveIfFound(m_euthanasyobjects, gameobj);
	CM_ListRemoveIfFound(m_tempObjectList, gameobj);

	if (gameobj == m_active_camera)
	{
		//no AddRef done on m_active_camera so no Release
		//m_active_camera->Release();
		m_active_camera = nullptr;
	}

	if (gameobj == m_overrideCullingCamera) {
		m_overrideCullingCamera = nullptr;
	}

	// return value will be 0 if the object is actually deleted (all reference gone)
	
	return ret;
}

KX_Camera* KX_Scene::GetActiveCamera()
{
	// nullptr if not defined
	return m_active_camera;
}

void KX_Scene::SetActiveCamera(KX_Camera* cam)
{
	m_active_camera = cam;
}

KX_Camera *KX_Scene::GetOverrideCullingCamera() const
{
	return m_overrideCullingCamera;
}

void KX_Scene::SetOverrideCullingCamera(KX_Camera *cam)
{
	m_overrideCullingCamera = cam;
}

void KX_Scene::SetCameraOnTop(KX_Camera* cam)
{
	// no release and addref just change camera place
	m_cameralist->RemoveValue(cam);
	m_cameralist->Add(cam);
}

void KX_Scene::PhysicsCullingCallback(KX_ClientObjectInfo *objectInfo, void *cullingInfo)
{
	CullingInfo *info = static_cast<CullingInfo *>(cullingInfo);
	KX_GameObject* gameobj = objectInfo->m_gameobject;
	if (!gameobj->GetVisible() || !gameobj->UseCulling()) {
		// ideally, invisible objects should be removed from the culling tree temporarily
		return;
	}
	if (info->m_layer && !(gameobj->GetLayer() & info->m_layer)) {
		// used for shadow: object is not in shadow layer
		return;
	}

	// make object visible
	gameobj->SetCulled(false);
	info->m_objects.push_back(gameobj);
}

void KX_Scene::CalculateVisibleMeshes(std::vector<KX_GameObject *>& objects, KX_Camera *cam, int layer)
{
	if (!cam->GetFrustumCulling()) {
		for (KX_GameObject *gameobj : m_objectlist) {
			gameobj->GetCullingNode()->SetCulled(false);
			objects.push_back(gameobj);
		}
		return;
	}

	CalculateVisibleMeshes(objects, cam->GetFrustum(), layer);
}

void KX_Scene::CalculateVisibleMeshes(std::vector<KX_GameObject *>& objects, const SG_Frustum& frustum, int layer)
{
	m_boundingBoxManager->Update(false);

	bool dbvt_culling = false;
	if (m_dbvt_culling) {
		for (KX_GameObject *gameobj : m_objectlist) {
			gameobj->SetCulled(true);
			/* Reset KX_GameObject m_culled to true before doing culling
			 * since DBVT culling will only set it to false.
			 */
			if (gameobj->GetDeformer()) {
				/** Update all the deformer, not only per material.
				 * One of the side effect is to clear some flags about AABB calculation.
				 * like in KX_SoftBodyDeformer.
				 */
				gameobj->GetDeformer()->UpdateBuckets();
			}
			// Update the object bounding volume box.
			gameobj->UpdateBounds(false);
		}

		// test culling through Bullet
		// get the clip planes
		const std::array<MT_Vector4, 6>& planes = frustum.GetPlanes();
		const MT_Matrix4x4& matrix = frustum.GetMatrix();
		const int *viewport = KX_GetActiveEngine()->GetCanvas()->GetViewPort();
		CullingInfo info(layer, objects);

		dbvt_culling = m_physicsEnvironment->CullingTest(PhysicsCullingCallback, &info, planes, m_dbvt_occlusion_res, viewport, matrix);
	}
	if (!dbvt_culling) {
		KX_CullingHandler handler(objects, frustum);
		for (KX_GameObject *gameobj : m_objectlist) {
			if (gameobj->UseCulling() && gameobj->GetVisible() && (layer == 0 || gameobj->GetLayer() & layer)) {
				if (gameobj->GetDeformer()) {
					/** Update all the deformer, not only per material.
					 * One of the side effect is to clear some flags about AABB calculation.
					 * like in KX_SoftBodyDeformer.
					 */
					gameobj->GetDeformer()->UpdateBuckets();
				}
				// Update the object bounding volume box.
				gameobj->UpdateBounds(false);

				handler.Process(gameobj);
			}
		}
	}

	m_boundingBoxManager->ClearModified();
}

void KX_Scene::DrawDebug(RAS_DebugDraw& debugDraw, const std::vector<KX_GameObject *>& objects)
{
	const KX_DebugOption showBoundingBox = KX_GetActiveEngine()->GetShowBoundingBox();
	if (showBoundingBox != KX_DebugOption::DISABLE) {
		for (KX_GameObject *gameobj : m_objectlist) {
			const MT_Vector3& scale = gameobj->NodeGetWorldScaling();
			const MT_Vector3& position = gameobj->NodeGetWorldPosition();
			const MT_Matrix3x3& orientation = gameobj->NodeGetWorldOrientation();
			const SG_BBox& box = gameobj->GetCullingNode()->GetAabb();
			const MT_Vector3& center = box.GetCenter();

			debugDraw.DrawAabb(position, orientation, box.GetMin() * scale, box.GetMax() * scale,
				MT_Vector4(1.0f, 0.0f, 1.0f, 1.0f));

			// Render center in red, green and blue.
			debugDraw.DrawLine(orientation * center * scale + position,
				orientation * (center + MT_Vector3(1.0f, 0.0f, 0.0f)) * scale + position,
				MT_Vector4(1.0f, 0.0f, 0.0f, 1.0f));
			debugDraw.DrawLine(orientation * center * scale + position,
				orientation * (center + MT_Vector3(0.0f, 1.0f, 0.0f)) * scale  + position,
				MT_Vector4(0.0f, 1.0f, 0.0f, 1.0f));
			debugDraw.DrawLine(orientation * center * scale + position,
				orientation * (center + MT_Vector3(0.0f, 0.0f, 1.0f)) * scale  + position,
				MT_Vector4(0.0f, 0.0f, 1.0f, 1.0f));
		}
	}

	const KX_DebugOption showArmatures = KX_GetActiveEngine()->GetShowArmatures();
	if (showArmatures != KX_DebugOption::DISABLE) {
		// The side effect of a armature is that it was added in the animated object list.
		for (KX_GameObject *gameobj : m_animatedlist) {
			if (gameobj->GetGameObjectType() == SCA_IObject::OBJ_ARMATURE) {
				BL_ArmatureObject *armature = (BL_ArmatureObject *)gameobj;
				if (showArmatures == KX_DebugOption::FORCE || armature->GetDrawDebug()) {
					armature->DrawDebug(debugDraw);
				}
			}
		}
	}
}

void KX_Scene::RenderDebugProperties(RAS_DebugDraw& debugDraw, int xindent, int ysize, int& xcoord, int& ycoord, unsigned short propsMax)
{
	static const MT_Vector4 white(1.0f, 1.0f, 1.0f, 1.0f);

	// The 'normal' debug props.
	const std::vector<SCA_DebugProp>& debugproplist = GetDebugProperties();

	unsigned short numprop = debugproplist.size();
	if (numprop > propsMax) {
		numprop = propsMax;
	}

	for (unsigned i = 0; i < numprop; ++i) {
		const SCA_DebugProp& debugProp = debugproplist[i];
		SCA_IObject *gameobj = debugProp.m_obj;
		const std::string objname = gameobj->GetName();
		const std::string& propname = debugProp.m_name;
		if (propname == "__state__") {
			// reserve name for object state
			unsigned int state = gameobj->GetState();
			std::string debugtxt = objname + "." + propname + " = ";
			bool first = true;
			for (int statenum = 1; state; state >>= 1, statenum++) {
				if (state & 1) {
					if (!first) {
						debugtxt += ",";
					}
					debugtxt += std::to_string(statenum);
					first = false;
				}
			}
			debugDraw.RenderText2D(debugtxt, MT_Vector2(xcoord + xindent, ycoord), white);
			ycoord += ysize;
		}
		else {
			EXP_Value *propval = gameobj->GetProperty(propname);
			if (propval) {
				const std::string text = propval->GetText();
				const std::string debugtxt = objname + ": '" + propname + "' = " + text;
				debugDraw.RenderText2D(debugtxt, MT_Vector2(xcoord + xindent, ycoord), white);
				ycoord += ysize;
			}
		}
	}
}

// logic stuff
void KX_Scene::LogicBeginFrame(double curtime, double framestep)
{
	// have a look at temp objects ...
	for (KX_GameObject *gameobj : m_tempObjectList) {
		EXP_FloatValue* propval = (EXP_FloatValue *)gameobj->GetProperty("::timebomb");
		
		if (propval) {
			const float timeleft = propval->GetNumber() - framestep;
			
			if (timeleft > 0) {
				propval->SetFloat(timeleft);
			}
			else {
				// Remove obj, remove the object from tempObjectList in NewRemoveObject only.
				DelayedRemoveObject(gameobj);
			}
		}
		else {
			// All object is the tempObjectList should have a clock.
			BLI_assert(false);
		}
	}
	m_logicmgr->BeginFrame(curtime, framestep);
}

void KX_Scene::AddAnimatedObject(KX_GameObject *gameobj)
{
	CM_ListAddIfNotFound(m_animatedlist, gameobj);
}

static void update_anim_thread_func(TaskPool *pool, void *taskdata, int UNUSED(threadid))
{
	KX_GameObject *gameobj, *parent;
	bool needs_update;
	KX_Scene::AnimationPoolData *data = (KX_Scene::AnimationPoolData *)BLI_task_pool_userdata(pool);
	double curtime = data->curtime;

	gameobj = (KX_GameObject*)taskdata;

	// Non-armature updates are fast enough, so just update them
	needs_update = gameobj->GetGameObjectType() != SCA_IObject::OBJ_ARMATURE;

	if (!needs_update) {
		// If we got here, we're looking to update an armature, so check its children meshes
		// to see if we need to bother with a more expensive pose update
		const std::vector<KX_GameObject *> children = gameobj->GetChildren();

		bool has_mesh = false, has_non_mesh = false;

		// Check for meshes that haven't been culled
		for (KX_GameObject *child : children) {
			if (!child->GetCulled()) {
				needs_update = true;
				break;
			}

			if (child->GetMeshList().empty())
				has_non_mesh = true;
			else
				has_mesh = true;
		}

		// If we didn't find a non-culled mesh, check to see
		// if we even have any meshes, and update if this
		// armature has only non-mesh children.
		if (!needs_update && !has_mesh && has_non_mesh)
			needs_update = true;
	}

	// If the object is a culled armature, then we manage only the animation time and end of its animations.
	gameobj->UpdateActionManager(curtime, needs_update);

	if (needs_update) {
		const std::vector<KX_GameObject *> children = gameobj->GetChildren();
		parent = gameobj->GetParent();

		// Only do deformers here if they are not parented to an armature, otherwise the armature will
		// handle updating its children
		if (gameobj->GetDeformer() && (!parent || parent->GetGameObjectType() != SCA_IObject::OBJ_ARMATURE))
			gameobj->GetDeformer()->Update();

		for (KX_GameObject *child : children) {
			if (child->GetDeformer()) {
				child->GetDeformer()->Update();
			}
		}
	}
}

void KX_Scene::UpdateAnimations(double curtime)
{
	m_animationPoolData.curtime = curtime;

	for (KX_GameObject *gameobj : m_animatedlist) {
		if (!gameobj->IsActionsSuspended()) {
			BLI_task_pool_push(m_animationPool, update_anim_thread_func, gameobj, false, TASK_PRIORITY_LOW);
		}
	}

	BLI_task_pool_work_and_wait(m_animationPool);
}

void KX_Scene::LogicUpdateFrame(double curtime)
{
	m_componentManager.UpdateComponents();

	m_logicmgr->UpdateFrame(curtime);
}

void KX_Scene::LogicEndFrame()
{
	m_logicmgr->EndFrame();

	/* Don't remove the objects from the euthanasy list here as the child objects of a deleted
	 * parent object are destructed directly from the sgnode in the same time the parent
	 * object is destructed. These child objects must be removed automatically from the
	 * euthanasy list to avoid double deletion in case the user ask to delete the child object
	 * explicitly. NewRemoveObject is the place to do it.
	 */
	while (!m_euthanasyobjects.empty()) {
		RemoveObject(m_euthanasyobjects.front());
	}

	//prepare obstacle simulation for new frame
	if (m_obstacleSimulation)
		m_obstacleSimulation->UpdateObstacles();

	for (KX_FontObject *font : m_fontlist) {
		font->UpdateTextFromProperty();
	}
}



/**
 * UpdateParents: SceneGraph transformation update.
 */
void KX_Scene::UpdateParents(double curtime)
{
	// we use the SG dynamic list
	SG_Node* node;

	while ((node = SG_Node::GetNextScheduled(m_sghead)) != nullptr)
	{
		node->UpdateWorldData(curtime);
	}

	// the list must be empty here
	BLI_assert(m_sghead.Empty());
	// some nodes may be ready for reschedule, move them to schedule list for next time
	while ((node = SG_Node::GetNextRescheduled(m_sghead)) != nullptr)
	{
		node->Schedule(m_sghead);
	}
}


RAS_MaterialBucket* KX_Scene::FindBucket(class RAS_IPolyMaterial* polymat, bool &bucketCreated)
{
	return m_bucketmanager->FindBucket(polymat, bucketCreated);
}



void KX_Scene::RenderBuckets(const std::vector<KX_GameObject *>& objects, RAS_Rasterizer::DrawType drawingMode, const MT_Transform& cameratransform,
		RAS_Rasterizer *rasty, RAS_OffScreen *offScreen)
{
	for (KX_GameObject *gameobj : objects) {
		/* This function update all mesh slot info (e.g culling, color, matrix) from the game object.
		 * It's done just before the render to be sure of the object color and visibility. */
		gameobj->UpdateBuckets();
	}

	m_bucketmanager->Renderbuckets(drawingMode, cameratransform, rasty, offScreen);
	KX_BlenderMaterial::EndFrame(rasty);
}

void KX_Scene::RenderTextureRenderers(KX_TextureRendererManager::RendererCategory category, RAS_Rasterizer *rasty,
		RAS_OffScreen *offScreen, KX_Camera *camera, const RAS_Rect& viewport, const RAS_Rect& area)
{
	m_rendererManager->Render(category, rasty, offScreen, camera, viewport, area);
}

void KX_Scene::UpdateObjectLods(KX_Camera *cam, const std::vector<KX_GameObject *>& objects)
{
	const MT_Vector3& cam_pos = cam->NodeGetWorldPosition();
	const float lodfactor = cam->GetLodDistanceFactor();

	for (KX_GameObject *gameobj : objects) {
		gameobj->UpdateLod(cam_pos, lodfactor);
	}
}

void KX_Scene::SetLodHysteresis(bool active)
{
	m_isActivedHysteresis = active;
}

bool KX_Scene::IsActivedLodHysteresis(void)
{
	return m_isActivedHysteresis;
}

void KX_Scene::SetLodHysteresisValue(int hysteresisvalue)
{
	m_lodHysteresisValue = hysteresisvalue;
}

int KX_Scene::GetLodHysteresisValue(void)
{
	return m_lodHysteresisValue;
}

void KX_Scene::UpdateObjectActivity()
{
	if (!m_activityCulling) {
		return;
	}

	std::vector<MT_Vector3> camPositions;

	for (KX_Camera *cam : m_cameralist) {
		if (cam->GetActivityCulling()) {
			camPositions.push_back(cam->NodeGetWorldPosition());
		}
	}

	// None cameras are using object activity culling?
	if (camPositions.size() == 0) {
		return;
	}

	for (KX_GameObject *gameobj : m_objectlist) {
		// If the object doesn't manage activity culling we don't compute distance.
		if (gameobj->GetActivityCullingInfo().m_flags & KX_GameObject::ActivityCullingInfo::ACTIVITY_NONE) {
			continue;
		}

		// For each camera compute the distance to objects and keep the minimum distance.
		const MT_Vector3& obpos = gameobj->NodeGetWorldPosition();
		float dist = FLT_MAX;
		for (const MT_Vector3& campos : camPositions) {
			// Keep the minimum distance.
			dist = std::min(obpos.distance2(campos), dist);
		}
		gameobj->UpdateActivity(dist);
	}
}

KX_NetworkMessageScene* KX_Scene::GetNetworkMessageScene()
{
	return m_networkScene;
}

void KX_Scene::SetNetworkMessageScene(KX_NetworkMessageScene *newScene)
{
	m_networkScene = newScene;
}

void	KX_Scene::SetGravity(const MT_Vector3& gravity)
{
	GetPhysicsEnvironment()->SetGravity(gravity[0],gravity[1],gravity[2]);
}

MT_Vector3 KX_Scene::GetGravity()
{
	MT_Vector3 gravity;

	GetPhysicsEnvironment()->GetGravity(gravity);

	return gravity;
}

void KX_Scene::SetPhysicsEnvironment(class PHY_IPhysicsEnvironment* physEnv)
{
	m_physicsEnvironment = physEnv;
	if (m_physicsEnvironment) {
		KX_CollisionEventManager* collisionmgr = new KX_CollisionEventManager(m_logicmgr, physEnv);
		m_logicmgr->RegisterEventManager(collisionmgr);
	}
}
 
void KX_Scene::SetSuspendedDelta(double suspendeddelta)
{
	m_suspendeddelta = suspendeddelta;
}
double KX_Scene::GetSuspendedDelta() const
{
	return m_suspendeddelta;
}

short KX_Scene::GetAnimationFPS()
{
	return m_blenderScene->r.frs_sec;
}

static void MergeScene_LogicBrick(SCA_ILogicBrick* brick, KX_Scene *from, KX_Scene *to)
{
	SCA_LogicManager *logicmgr= to->GetLogicManager();

	brick->Replace_IScene(to);
	brick->Replace_NetworkScene(to->GetNetworkMessageScene());
	brick->SetLogicManager(to->GetLogicManager());

	// If we end up replacing a KX_CollisionEventManager, we need to make sure
	// physics controllers are properly in place. In other words, do this
	// after merging physics controllers!
	SCA_ISensor *sensor=  dynamic_cast<class SCA_ISensor *>(brick);
	if (sensor) {
		sensor->Replace_EventManager(logicmgr);
	}

	SCA_2DFilterActuator *filter_actuator = dynamic_cast<class SCA_2DFilterActuator*>(brick);
	if (filter_actuator) {
		filter_actuator->SetScene(to, to->Get2DFilterManager());
	}
}

static void MergeScene_GameObject(KX_GameObject* gameobj, KX_Scene *to, KX_Scene *from)
{
	SCA_ActuatorList& actuators = gameobj->GetActuators();
	for (SCA_IActuator *actuator : actuators) {
		MergeScene_LogicBrick(actuator, from, to);
	}

	SCA_SensorList& sensors = gameobj->GetSensors();
	for (SCA_ISensor *sensor : sensors) {
		MergeScene_LogicBrick(sensor, from, to);
	}

	SCA_ControllerList& controllers = gameobj->GetControllers();
	for (SCA_IController *controller : controllers) {
		MergeScene_LogicBrick(controller, from, to);
	}

	/* graphics controller */
	PHY_IController *ctrl = gameobj->GetGraphicController();
	if (ctrl) {
		/* SHOULD update the m_cullingTree */
		ctrl->SetPhysicsEnvironment(to->GetPhysicsEnvironment());
	}

	ctrl = gameobj->GetPhysicsController();
	if (ctrl) {
		ctrl->SetPhysicsEnvironment(to->GetPhysicsEnvironment());
	}

	/* SG_Node can hold a scene reference */
	SG_Node *sg= gameobj->GetSGNode();
	if (sg) {
		if (sg->GetSGClientInfo() == from) {
			sg->SetSGClientInfo(to);

			/* Make sure to grab the children too since they might not be tied to a game object */
			const NodeList& children = sg->GetSGChildren();
			for (SG_Node *child : children) {
				child->SetSGClientInfo(to);
			}
		}
	}
	/* If the object is a light, update it's scene */
	if (gameobj->GetGameObjectType() == SCA_IObject::OBJ_LIGHT)
		((KX_LightObject*)gameobj)->UpdateScene(to);

	// All armatures should be in the animated object list to be umpdated.
	if (gameobj->GetGameObjectType() == SCA_IObject::OBJ_ARMATURE)
		to->AddAnimatedObject(gameobj);

	/* Add the object to the scene's logic manager */
	to->GetLogicManager()->RegisterGameObjectName(gameobj->GetName(), gameobj);
	to->GetLogicManager()->RegisterGameObj(gameobj->GetBlenderObject(), gameobj);

	for (RAS_MeshObject *meshobj : gameobj->GetMeshList()) {
		// Register the mesh object by name and blender object.
		to->GetLogicManager()->RegisterGameMeshName(meshobj->GetName(), gameobj->GetBlenderObject());
		to->GetLogicManager()->RegisterMeshName(meshobj->GetName(), meshobj);
	}
}

bool KX_Scene::MergeScene(KX_Scene *other)
{
	PHY_IPhysicsEnvironment *env = this->GetPhysicsEnvironment();
	PHY_IPhysicsEnvironment *env_other = other->GetPhysicsEnvironment();

	if ((env==nullptr) != (env_other==nullptr)) /* TODO - even when both scenes have NONE physics, the other is loaded with bullet enabled, ??? */
	{
		CM_FunctionError("physics scenes type differ, aborting\n\tsource " << (int)(env!=nullptr) << ", target " << (int)(env_other!=nullptr));
		return false;
	}

	GetBucketManager()->MergeBucketManager(other->GetBucketManager(), this);
	GetBoundingBoxManager()->Merge(other->GetBoundingBoxManager());
	GetTextureRendererManager()->Merge(other->GetTextureRendererManager());

	/* active + inactive == all ??? - lets hope so */
	for (KX_GameObject *gameobj : *other->GetObjectList()) {
		MergeScene_GameObject(gameobj, this, other);

		/* add properties to debug list for LibLoad objects */
		if (KX_GetActiveEngine()->GetFlag(KX_KetsjiEngine::AUTO_ADD_DEBUG_PROPERTIES)) {
			AddObjectDebugProperties(gameobj);
		}
	}

	for (KX_GameObject *gameobj : *other->GetInactiveList()) {
		MergeScene_GameObject(gameobj, this, other);
	}

	if (env) {
		env->MergeEnvironment(env_other);
		EXP_ListValue<KX_GameObject> *otherObjects = other->GetObjectList();

		// List of all physics objects to merge (needed by ReplicateConstraints).
		std::vector<KX_GameObject *> physicsObjects;
		for (KX_GameObject *gameobj : *otherObjects) {
			if (gameobj->GetPhysicsController()) {
				physicsObjects.push_back(gameobj);
			}
		}

		for (unsigned int i = 0; i < physicsObjects.size(); ++i) {
			KX_GameObject *gameobj = physicsObjects[i];
			// Replicate all constraints in the right physics environment.
			gameobj->GetPhysicsController()->ReplicateConstraints(gameobj, physicsObjects);
			gameobj->ClearConstraints();
		}
	}


	GetObjectList()->MergeList(other->GetObjectList());
	other->GetObjectList()->ReleaseAndRemoveAll();

	GetInactiveList()->MergeList(other->GetInactiveList());
	other->GetInactiveList()->ReleaseAndRemoveAll();

	GetRootParentList()->MergeList(other->GetRootParentList());
	other->GetRootParentList()->ReleaseAndRemoveAll();

	GetLightList()->MergeList(other->GetLightList());
	other->GetLightList()->ReleaseAndRemoveAll();

	GetCameraList()->MergeList(other->GetCameraList());
	other->GetCameraList()->ReleaseAndRemoveAll();

	GetFontList()->MergeList(other->GetFontList());
	other->GetFontList()->ReleaseAndRemoveAll();

	/* merge logic */
	{
		SCA_LogicManager *logicmgr=			GetLogicManager();
		SCA_LogicManager *logicmgr_other=	other->GetLogicManager();

		std::vector<class SCA_EventManager*>evtmgrs= logicmgr->GetEventManagers();
		//vector<class SCA_EventManager*>evtmgrs_others= logicmgr_other->GetEventManagers();

		//SCA_EventManager *evtmgr;
		SCA_EventManager *evtmgr_other;

		for (unsigned int i= 0; i < evtmgrs.size(); i++) {
			evtmgr_other= logicmgr_other->FindEventManager(evtmgrs[i]->GetType());

			if (evtmgr_other) /* unlikely but possible one scene has a joystick and not the other */
				evtmgr_other->Replace_LogicManager(logicmgr);

			/* when merging objects sensors are moved across into the new manager, don't need to do this here */
		}

		/* grab any timer properties from the other scene */
		SCA_TimeEventManager *timemgr=		GetTimeEventManager();
		SCA_TimeEventManager *timemgr_other=	other->GetTimeEventManager();
		std::vector<EXP_Value*> times = timemgr_other->GetTimeValues();

		for (unsigned int i= 0; i < times.size(); i++) {
			timemgr->AddTimeProperty(times[i]);
		}
		
	}
	return true;
}

RAS_2DFilterManager *KX_Scene::Get2DFilterManager() const
{
	return m_filterManager;
}

RAS_OffScreen *KX_Scene::Render2DFilters(RAS_Rasterizer *rasty, RAS_ICanvas *canvas, RAS_OffScreen *inputofs, RAS_OffScreen *targetofs)
{
	return m_filterManager->RenderFilters(rasty, canvas, inputofs, targetofs);
}

KX_ObstacleSimulation *KX_Scene::GetObstacleSimulation()
{
	return m_obstacleSimulation;
}

void KX_Scene::SetObstacleSimulation(KX_ObstacleSimulation *obstacleSimulation)
{
	m_obstacleSimulation = obstacleSimulation;
}

#ifdef WITH_PYTHON

void KX_Scene::RunDrawingCallbacks(DrawingCallbackType callbackType, KX_Camera *camera)
{
	PyObject *list = m_drawCallbacks[callbackType];
	if (!list || PyList_GET_SIZE(list) == 0) {
		return;
	}

	if (camera) {
		PyObject *args[1] = {camera->GetProxy()};
		EXP_RunPythonCallBackList(list, args, 0, 1);
	}
	else {
		EXP_RunPythonCallBackList(list, nullptr, 0, 0);
	}
}

//----------------------------------------------------------------------------
//Python

PyTypeObject KX_Scene::Type = {
	PyVarObject_HEAD_INIT(nullptr, 0)
	"KX_Scene",
	sizeof(EXP_PyObjectPlus_Proxy),
	0,
	py_base_dealloc,
	0,
	0,
	0,
	0,
	py_base_repr,
	0,
	&Sequence,
	&Mapping,
	0,0,0,0,0,0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	0,0,0,0,0,0,0,
	Methods,
	0,
	0,
	&EXP_Value::Type,
	0,0,0,0,0,0,
	py_base_new
};

PyMethodDef KX_Scene::Methods[] = {
	EXP_PYMETHODTABLE(KX_Scene, addObject),
	EXP_PYMETHODTABLE(KX_Scene, end),
	EXP_PYMETHODTABLE(KX_Scene, restart),
	EXP_PYMETHODTABLE(KX_Scene, replace),
	EXP_PYMETHODTABLE(KX_Scene, suspend),
	EXP_PYMETHODTABLE(KX_Scene, resume),
	EXP_PYMETHODTABLE(KX_Scene, drawObstacleSimulation),
	
	/* dict style access */
	EXP_PYMETHODTABLE(KX_Scene, get),
	
	{nullptr,nullptr} //Sentinel
};
static PyObject *Map_GetItem(PyObject *self_v, PyObject *item)
{
	KX_Scene* self = static_cast<KX_Scene*>EXP_PROXY_REF(self_v);
	const char *attr_str= _PyUnicode_AsString(item);
	PyObject *pyconvert;
	
	if (self == nullptr) {
		PyErr_SetString(PyExc_SystemError, "val = scene[key]: KX_Scene, " EXP_PROXY_ERROR_MSG);
		return nullptr;
	}

	if (!self->m_attr_dict)
		self->m_attr_dict = PyDict_New();
	
	if (self->m_attr_dict && (pyconvert=PyDict_GetItem(self->m_attr_dict, item))) {
		
		if (attr_str)
			PyErr_Clear();
		Py_INCREF(pyconvert);
		return pyconvert;
	}
	else {
		if (attr_str)	PyErr_Format(PyExc_KeyError, "value = scene[key]: KX_Scene, key \"%s\" does not exist", attr_str);
		else			PyErr_SetString(PyExc_KeyError, "value = scene[key]: KX_Scene, key does not exist");
		return nullptr;
	}
		
}

static int Map_SetItem(PyObject *self_v, PyObject *key, PyObject *val)
{
	KX_Scene* self = static_cast<KX_Scene*>EXP_PROXY_REF(self_v);
	const char *attr_str= _PyUnicode_AsString(key);
	if (attr_str==nullptr)
		PyErr_Clear();
	
	if (self == nullptr) {
		PyErr_SetString(PyExc_SystemError, "scene[key] = value: KX_Scene, " EXP_PROXY_ERROR_MSG);
		return -1;
	}

	if (!self->m_attr_dict)
		self->m_attr_dict = PyDict_New();

	if (val==nullptr) { /* del ob["key"] */
		int del= 0;
		
		if (self->m_attr_dict)
			del |= (PyDict_DelItem(self->m_attr_dict, key)==0) ? 1:0;
		
		if (del==0) {
			if (attr_str)	PyErr_Format(PyExc_KeyError, "scene[key] = value: KX_Scene, key \"%s\" could not be set", attr_str);
			else			PyErr_SetString(PyExc_KeyError, "del scene[key]: KX_Scene, key could not be deleted");
			return -1;
		}
		else if (self->m_attr_dict) {
			PyErr_Clear(); /* PyDict_DelItem sets an error when it fails */
		}
	}
	else { /* ob["key"] = value */
		int set = 0;

		if (self->m_attr_dict==nullptr) /* lazy init */
			self->m_attr_dict= PyDict_New();
		
		
		if (PyDict_SetItem(self->m_attr_dict, key, val)==0)
			set= 1;
		else
			PyErr_SetString(PyExc_KeyError, "scene[key] = value: KX_Scene, key not be added to internal dictionary");
	
		if (set==0)
			return -1; /* pythons error value */
		
	}
	
	return 0; /* success */
}

static int Seq_Contains(PyObject *self_v, PyObject *value)
{
	KX_Scene* self = static_cast<KX_Scene*>EXP_PROXY_REF(self_v);
	
	if (self == nullptr) {
		PyErr_SetString(PyExc_SystemError, "val in scene: KX_Scene, " EXP_PROXY_ERROR_MSG);
		return -1;
	}

	if (!self->m_attr_dict)
		self->m_attr_dict = PyDict_New();

	if (self->m_attr_dict && PyDict_GetItem(self->m_attr_dict, value))
		return 1;
	
	return 0;
}

PyMappingMethods KX_Scene::Mapping = {
	(lenfunc)nullptr,                  /* inquiry mp_length */
	(binaryfunc)Map_GetItem,        /* binaryfunc mp_subscript */
	(objobjargproc)Map_SetItem,     /* objobjargproc mp_ass_subscript */
};

PySequenceMethods KX_Scene::Sequence = {
	nullptr,                       /* Cant set the len otherwise it can evaluate as false */
	nullptr,                       /* sq_concat */
	nullptr,                       /* sq_repeat */
	nullptr,                       /* sq_item */
	nullptr,                       /* sq_slice */
	nullptr,                       /* sq_ass_item */
	nullptr,                       /* sq_ass_slice */
	(objobjproc)Seq_Contains,   /* sq_contains */
	(binaryfunc) nullptr,          /* sq_inplace_concat */
	(ssizeargfunc) nullptr,        /* sq_inplace_repeat */
};

PyObject *KX_Scene::pyattr_get_name(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_Scene* self = static_cast<KX_Scene*>(self_v);
	return PyUnicode_FromStdString(self->GetName());
}

PyObject *KX_Scene::pyattr_get_objects(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_Scene* self = static_cast<KX_Scene*>(self_v);
	return self->GetObjectList()->GetProxy();
}

PyObject *KX_Scene::pyattr_get_objects_inactive(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_Scene* self = static_cast<KX_Scene*>(self_v);
	return self->GetInactiveList()->GetProxy();
}

PyObject *KX_Scene::pyattr_get_lights(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_Scene* self = static_cast<KX_Scene*>(self_v);
	return self->GetLightList()->GetProxy();
}

PyObject *KX_Scene::pyattr_get_filter_manager(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_Scene *self = static_cast<KX_Scene*>(self_v);
	KX_2DFilterManager *filterManager = (KX_2DFilterManager *)self->Get2DFilterManager();

	return filterManager->GetProxy();
}

PyObject *KX_Scene::pyattr_get_world(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_Scene* self = static_cast<KX_Scene*>(self_v);
	KX_WorldInfo *world = self->GetWorldInfo();

	if (world->GetName() != "") {
		return world->GetProxy();
	}
	else {
		Py_RETURN_NONE;
	}
}

PyObject *KX_Scene::pyattr_get_texts(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_Scene *self = static_cast<KX_Scene *>(self_v);
	return self->GetFontList()->GetProxy();
}

PyObject *KX_Scene::pyattr_get_cameras(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_Scene *self = static_cast<KX_Scene *>(self_v);
	return self->GetCameraList()->GetProxy();
}

PyObject *KX_Scene::pyattr_get_active_camera(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_Scene* self = static_cast<KX_Scene*>(self_v);
	KX_Camera* cam= self->GetActiveCamera();
	if (cam)
		return self->GetActiveCamera()->GetProxy();
	else
		Py_RETURN_NONE;
}

int KX_Scene::pyattr_set_active_camera(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef, PyObject *value)
{
	KX_Scene* self = static_cast<KX_Scene*>(self_v);
	KX_Camera *camOb;
	
	if (!ConvertPythonToCamera(self, value, &camOb, false, "scene.active_camera = value: KX_Scene"))
		return PY_SET_ATTR_FAIL;
	
	self->SetActiveCamera(camOb);
	return PY_SET_ATTR_SUCCESS;
}

PyObject *KX_Scene::pyattr_get_overrideCullingCamera(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_Scene *self = static_cast<KX_Scene*>(self_v);
	KX_Camera *cam = self->GetOverrideCullingCamera();
	return (cam) ? cam->GetProxy() : Py_None;
}

int KX_Scene::pyattr_set_overrideCullingCamera(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef, PyObject *value)
{
	KX_Scene *self = static_cast<KX_Scene*>(self_v);
	KX_Camera *cam;

	if (!ConvertPythonToCamera(self, value, &cam, true, "scene.active_camera = value: KX_Scene")) {
		return PY_SET_ATTR_FAIL;
	}

	self->SetOverrideCullingCamera(cam);
	return PY_SET_ATTR_SUCCESS;
}

static std::map<const std::string, KX_Scene::DrawingCallbackType> callbacksTable = {
	{"pre_draw", KX_Scene::PRE_DRAW},
	{"pre_draw_setup", KX_Scene::PRE_DRAW_SETUP},
	{"post_draw", KX_Scene::POST_DRAW}
};

PyObject *KX_Scene::pyattr_get_drawing_callback(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_Scene *self = static_cast<KX_Scene *>(self_v);

	const DrawingCallbackType type = callbacksTable[attrdef->m_name];
	if (!self->m_drawCallbacks[type]) {
		self->m_drawCallbacks[type] = PyList_New(0);
	}

	Py_INCREF(self->m_drawCallbacks[type]);

	return self->m_drawCallbacks[type];
}

int KX_Scene::pyattr_set_drawing_callback(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef, PyObject *value)
{
	KX_Scene *self = static_cast<KX_Scene *>(self_v);

	if (!PyList_CheckExact(value)) {
		PyErr_SetString(PyExc_ValueError, "Expected a list");
		return PY_SET_ATTR_FAIL;
	}

	const DrawingCallbackType type = callbacksTable[attrdef->m_name];

	Py_XDECREF(self->m_drawCallbacks[type]);

	Py_INCREF(value);
	self->m_drawCallbacks[type] = value;

	return PY_SET_ATTR_SUCCESS;
}

PyObject *KX_Scene::pyattr_get_gravity(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_Scene* self = static_cast<KX_Scene*>(self_v);

	return PyObjectFrom(self->GetGravity());
}

int KX_Scene::pyattr_set_gravity(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef, PyObject *value)
{
	KX_Scene* self = static_cast<KX_Scene*>(self_v);

	MT_Vector3 vec;
	if (!PyVecTo(value, vec))
		return PY_SET_ATTR_FAIL;

	self->SetGravity(vec);
	return PY_SET_ATTR_SUCCESS;
}

PyAttributeDef KX_Scene::Attributes[] = {
	EXP_PYATTRIBUTE_RO_FUNCTION("name",				KX_Scene, pyattr_get_name),
	EXP_PYATTRIBUTE_RO_FUNCTION("objects",			KX_Scene, pyattr_get_objects),
	EXP_PYATTRIBUTE_RO_FUNCTION("objectsInactive",	KX_Scene, pyattr_get_objects_inactive),
	EXP_PYATTRIBUTE_RO_FUNCTION("lights",			KX_Scene, pyattr_get_lights),
	EXP_PYATTRIBUTE_RO_FUNCTION("texts",				KX_Scene, pyattr_get_texts),
	EXP_PYATTRIBUTE_RO_FUNCTION("cameras",			KX_Scene, pyattr_get_cameras),
	EXP_PYATTRIBUTE_RO_FUNCTION("filterManager",		KX_Scene, pyattr_get_filter_manager),
	EXP_PYATTRIBUTE_RO_FUNCTION("world",				KX_Scene, pyattr_get_world),
	EXP_PYATTRIBUTE_RW_FUNCTION("active_camera",		KX_Scene, pyattr_get_active_camera, pyattr_set_active_camera),
	EXP_PYATTRIBUTE_RW_FUNCTION("overrideCullingCamera",KX_Scene, pyattr_get_overrideCullingCamera, pyattr_set_overrideCullingCamera),
	EXP_PYATTRIBUTE_RW_FUNCTION("pre_draw",			KX_Scene, pyattr_get_drawing_callback, pyattr_set_drawing_callback),
	EXP_PYATTRIBUTE_RW_FUNCTION("post_draw",			KX_Scene, pyattr_get_drawing_callback, pyattr_set_drawing_callback),
	EXP_PYATTRIBUTE_RW_FUNCTION("pre_draw_setup",	KX_Scene, pyattr_get_drawing_callback, pyattr_set_drawing_callback),
	EXP_PYATTRIBUTE_RW_FUNCTION("gravity",			KX_Scene, pyattr_get_gravity, pyattr_set_gravity),
	EXP_PYATTRIBUTE_BOOL_RO("suspended",				KX_Scene, m_suspend),
	EXP_PYATTRIBUTE_BOOL_RO("activityCulling",		KX_Scene, m_activityCulling),
	EXP_PYATTRIBUTE_BOOL_RO("dbvt_culling",			KX_Scene, m_dbvt_culling),
	EXP_PYATTRIBUTE_NULL	//Sentinel
};

EXP_PYMETHODDEF_DOC(KX_Scene, addObject,
"addObject(object, other, time=0)\n"
"Returns the added object.\n")
{
	PyObject *pyob, *pyreference = Py_None;
	KX_GameObject *ob, *reference;

	float time = 0.0f;

	if (!PyArg_ParseTuple(args, "O|Of:addObject", &pyob, &pyreference, &time))
		return nullptr;

	if (!ConvertPythonToGameObject(m_logicmgr, pyob, &ob, false, "scene.addObject(object, reference, time): KX_Scene (first argument)") ||
		!ConvertPythonToGameObject(m_logicmgr, pyreference, &reference, true, "scene.addObject(object, reference, time): KX_Scene (second argument)"))
		return nullptr;

	if (!m_inactivelist->SearchValue(ob)) {
		PyErr_Format(PyExc_ValueError, "scene.addObject(object, reference, time): KX_Scene (first argument): object must be in an inactive layer");
		return nullptr;
	}
	KX_GameObject *replica = AddReplicaObject(ob, reference, time);
	
	// release here because AddReplicaObject AddRef's
	// the object is added to the scene so we don't want python to own a reference
	replica->Release();
	return replica->GetProxy();
}

EXP_PYMETHODDEF_DOC(KX_Scene, end,
"end()\n"
"Removes this scene from the game.\n")
{
	
	KX_GetActiveEngine()->RemoveScene(m_sceneName);
	
	Py_RETURN_NONE;
}

EXP_PYMETHODDEF_DOC(KX_Scene, restart,
				   "restart()\n"
				   "Restarts this scene.\n")
{
	KX_GetActiveEngine()->ReplaceScene(m_sceneName, m_sceneName);
	
	Py_RETURN_NONE;
}

EXP_PYMETHODDEF_DOC(KX_Scene, replace,
				   "replace(newScene)\n"
                   "Replaces this scene with another one.\n"
                   "Return True if the new scene exists and scheduled for replacement, False otherwise.\n")
{
	char* name;
	
	if (!PyArg_ParseTuple(args, "s:replace", &name))
		return nullptr;
	
    if (KX_GetActiveEngine()->ReplaceScene(m_sceneName, name))
        Py_RETURN_TRUE;
	
    Py_RETURN_FALSE;
}

EXP_PYMETHODDEF_DOC(KX_Scene, suspend,
					"suspend()\n"
					"Suspends this scene.\n")
{
	Suspend();
	
	Py_RETURN_NONE;
}

EXP_PYMETHODDEF_DOC(KX_Scene, resume,
					"resume()\n"
					"Resumes this scene.\n")
{
	Resume();
	
	Py_RETURN_NONE;
}

EXP_PYMETHODDEF_DOC(KX_Scene, drawObstacleSimulation,
				   "drawObstacleSimulation()\n"
				   "Draw debug visualization of obstacle simulation.\n")
{
	if (GetObstacleSimulation())
		GetObstacleSimulation()->DrawObstacles();

	Py_RETURN_NONE;
}

/* Matches python dict.get(key, [default]) */
EXP_PYMETHODDEF_DOC(KX_Scene, get, "")
{
	PyObject *key;
	PyObject *def = Py_None;
	PyObject *ret;

	if (!PyArg_ParseTuple(args, "O|O:get", &key, &def))
		return nullptr;
	
	if (m_attr_dict && (ret=PyDict_GetItem(m_attr_dict, key))) {
		Py_INCREF(ret);
		return ret;
	}
	
	Py_INCREF(def);
	return def;
}

bool ConvertPythonToScene(PyObject *value, KX_Scene **scene, bool py_none_ok, const char *error_prefix)
{
	if (value == nullptr) {
		PyErr_Format(PyExc_TypeError, "%s, python pointer nullptr, should never happen", error_prefix);
		*scene = nullptr;
		return false;
	}

	if (value == Py_None) {
		*scene = nullptr;

		if (py_none_ok) {
			return true;
		}
		else {
			PyErr_Format(PyExc_TypeError, "%s, expected KX_Scene or a KX_Scene name, None is invalid", error_prefix);
			return false;
		}
	}

	if (PyUnicode_Check(value)) {
		*scene = (KX_Scene *)KX_GetActiveEngine()->CurrentScenes()->FindValue(std::string(_PyUnicode_AsString(value)));

		if (*scene) {
			return true;
		}
		else {
			PyErr_Format(PyExc_ValueError, "%s, requested name \"%s\" did not match any in game", error_prefix, _PyUnicode_AsString(value));
			return false;
		}
	}

	if (PyObject_TypeCheck(value, &KX_Scene::Type)) {
		*scene = static_cast<KX_Scene *>EXP_PROXY_REF(value);

		// Sets the error.
		if (*scene == nullptr) {
			PyErr_Format(PyExc_SystemError, "%s, " EXP_PROXY_ERROR_MSG, error_prefix);
			return false;
		}

		return true;
	}

	*scene = nullptr;

	if (py_none_ok) {
		PyErr_Format(PyExc_TypeError, "%s, expect a KX_Scene, a string or None", error_prefix);
	}
	else {
		PyErr_Format(PyExc_TypeError, "%s, expect a KX_Scene or a string", error_prefix);
	}

	return false;
}

#endif // WITH_PYTHON
