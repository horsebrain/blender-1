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
 */

/** \file gameengine/Converter/BL_ModifierDeformer.cpp
 *  \ingroup bgeconv
 */

#ifdef _MSC_VER
#  pragma warning (disable:4786)
#endif

#include "MEM_guardedalloc.h"
#include "BL_ModifierDeformer.h"
#include "BL_BlenderDataConversion.h"
#include <string>
#include "RAS_IPolygonMaterial.h"
#include "RAS_MaterialBucket.h"
#include "RAS_MeshObject.h"
#include "RAS_MeshUser.h"
#include "RAS_BoundingBox.h"

#include "DNA_armature_types.h"
#include "DNA_action_types.h"
#include "DNA_key_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_curve_types.h"
#include "DNA_modifier_types.h"
#include "DNA_scene_types.h"
#include "BLI_utildefines.h"
#include "BKE_armature.h"
#include "BKE_action.h"
#include "BKE_key.h"
#include "BKE_ipo.h"
#include "MT_Vector3.h"

extern "C" {
	#include "BKE_customdata.h"
	#include "BKE_DerivedMesh.h"
	#include "BKE_lattice.h"
	#include "BKE_modifier.h"
}

#include "BLI_blenlib.h"
#include "BLI_math.h"

BL_ModifierDeformer::~BL_ModifierDeformer()
{
	if (m_dm) {
		// deformedOnly is used as a user counter
		if (--m_dm->deformedOnly == 0) {
			m_dm->needsFree = 1;
			m_dm->release(m_dm);
		}
	}
}

RAS_Deformer *BL_ModifierDeformer::GetReplica()
{
	BL_ModifierDeformer *result;

	result = new BL_ModifierDeformer(*this);
	result->ProcessReplica();
	return result;
}

void BL_ModifierDeformer::ProcessReplica()
{
	/* Note! - This is not inherited from EXP_PyObjectPlus */
	BL_ShapeDeformer::ProcessReplica();
	if (m_dm) {
		// by default try to reuse mesh, deformedOnly is used as a user count
		m_dm->deformedOnly++;
	}
	// this will force an update and if the mesh cannot be reused, a new one will be created
	m_lastModifierUpdate = -1.0;
}

bool BL_ModifierDeformer::HasCompatibleDeformer(Object *ob)
{
	if (!ob->modifiers.first)
		return false;
	// soft body cannot use mesh modifiers
	if ((ob->gameflag & OB_SOFT_BODY) != 0)
		return false;
	ModifierData *md;
	for (md = (ModifierData *)ob->modifiers.first; md; md = md->next) {
		if (modifier_dependsOnTime(md))
			continue;
		if (!(md->mode & eModifierMode_Realtime))
			continue;
		/* armature modifier are handled by SkinDeformer, not ModifierDeformer */
		if (md->type == eModifierType_Armature)
			continue;
		return true;
	}
	return false;
}

bool BL_ModifierDeformer::HasArmatureDeformer(Object *ob)
{
	if (!ob->modifiers.first)
		return false;

	ModifierData *md = (ModifierData *)ob->modifiers.first;
	if (md->type == eModifierType_Armature)
		return true;

	return false;
}

void BL_ModifierDeformer::Update(void)
{
	/* execute the modifiers */
	Object *blendobj = m_gameobj->GetBlenderObject();
	/* hack: the modifiers require that the mesh is attached to the object
	 * It may not be the case here because of replace mesh actuator */
	Mesh *oldmesh = (Mesh *)blendobj->data;
	blendobj->data = m_bmesh;
	/* execute the modifiers */
	DerivedMesh *dm = mesh_create_derived_no_virtual(m_scene, blendobj, (float (*)[3])m_transverts.data(), CD_MASK_MESH);
	/* restore object data */
	blendobj->data = oldmesh;
	/* free the current derived mesh and replace, (dm should never be nullptr) */
	if (m_dm != nullptr) {
		// HACK! use deformedOnly as a user counter
		if (--m_dm->deformedOnly == 0) {
			m_dm->needsFree = 1;
			m_dm->release(m_dm);
		}
	}
	m_dm = dm;
	// get rid of temporary data
	m_dm->needsFree = 0;
	m_dm->release(m_dm);
	// HACK! use deformedOnly as a user counter
	m_dm->deformedOnly = 1;
	DM_update_materials(m_dm, blendobj);

	UpdateBounds();
	UpdateTransverts();

	m_lastModifierUpdate = m_gameobj->GetLastFrame();
}

bool BL_ModifierDeformer::NeedUpdate() const
{
	// static derived mesh are not updated
	return (BL_ShapeDeformer::NeedUpdate() || m_lastModifierUpdate != m_gameobj->GetLastFrame() || m_bDynamic || !m_dm);
}

void BL_ModifierDeformer::UpdateBounds()
{
	float min[3], max[3];
	INIT_MINMAX(min, max);
	m_dm->getMinMax(m_dm, min, max);
	m_boundingBox->SetAabb(MT_Vector3(min), MT_Vector3(max));
}

void BL_ModifierDeformer::UpdateTransverts()
{
	if (!m_dm) {
		return;
	}

	const unsigned short nummat = m_mesh->GetNumMaterials();
	std::vector<BL_MeshMaterial> mats(nummat);

	for (unsigned short i = 0; i < nummat; ++i) {
		RAS_MeshMaterial *meshmat = m_mesh->GetMeshMaterial(i);
		RAS_IDisplayArray *array = m_displayArrayList[i];
		array->Clear();

		RAS_IPolyMaterial *mat = meshmat->GetBucket()->GetPolyMaterial();
		mats[i] = {array, meshmat->GetBucket(), mat->IsVisible(), mat->IsTwoSided(), mat->IsCollider(), mat->IsWire()};
	}

	BL_ConvertDerivedMeshToArray(m_dm, m_bmesh, mats, m_mesh->GetLayersInfo());

	for (RAS_IDisplayArray *array : m_displayArrayList) {
		array->SetModifiedFlag(RAS_IDisplayArray::SIZE_MODIFIED);
		array->UpdateCache();
	}

	// Update object's AABB.
	if (m_gameobj->GetAutoUpdateBounds()) {
		UpdateBounds();
	}
}
