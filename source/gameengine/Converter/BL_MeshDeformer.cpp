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
 * Simple deformation controller that restores a mesh to its rest position
 */

/** \file gameengine/Converter/BL_MeshDeformer.cpp
 *  \ingroup bgeconv
 */

#ifdef _MSC_VER
/* This warning tells us about truncation of __long__ stl-generated names.
 * It can occasionally cause DevStudio to have internal compiler warnings. */
#  pragma warning( disable:4786 )
#endif

#include "RAS_IPolygonMaterial.h"
#include "RAS_DisplayArray.h"
#include "BL_DeformableGameObject.h"
#include "BL_MeshDeformer.h"
#include "RAS_BoundingBoxManager.h"
#include "RAS_MeshObject.h"
#include "RAS_MeshUser.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include <string>
#include "BLI_math.h"

void BL_MeshDeformer::Update()
{
	// For each display array
	for (RAS_IDisplayArray *array: m_displayArrayList) {
		if (array->GetModifiedFlag() == RAS_IDisplayArray::NONE_MODIFIED) {
			continue;
		}

		//	For each vertex
		for (unsigned int i = 0, size = array->GetVertexCount(); i < size; ++i) {
			RAS_IVertex *v = array->GetVertex(i);
			const RAS_VertexInfo& vinfo = array->GetVertexInfo(i);
			v->SetXYZ(m_bmesh->mvert[vinfo.getOrigIndex()].co);
		}

		array->SetModifiedFlag(RAS_IDisplayArray::POSITION_MODIFIED);
	}

	m_lastDeformUpdate = m_gameobj->GetLastFrame();
}

bool BL_MeshDeformer::NeedUpdate() const
{
	// Only apply once per frame if the mesh is actually modified.
	return (m_lastDeformUpdate != m_gameobj->GetLastFrame());
}

BL_MeshDeformer::BL_MeshDeformer(BL_DeformableGameObject *gameobj, Object *obj, RAS_MeshObject *meshobj)
	:RAS_Deformer(meshobj),
	m_bmesh((Mesh *)(obj->data)),
	m_objMesh(obj),
	m_gameobj(gameobj),
	m_lastDeformUpdate(-1.0)
{
	KX_Scene *scene = m_gameobj->GetScene();
	RAS_BoundingBoxManager *boundingBoxManager = scene->GetBoundingBoxManager();
	m_boundingBox = boundingBoxManager->CreateBoundingBox();
	// Set AABB default to mesh bounding box AABB.
	m_boundingBox->CopyAabb(m_mesh->GetBoundingBox());
}

BL_MeshDeformer::~BL_MeshDeformer()
{
}

void BL_MeshDeformer::ProcessReplica()
{
	RAS_Deformer::ProcessReplica();
	m_transverts.clear();
	m_transnors.clear();
	m_bDynamic = false;
	m_lastDeformUpdate = -1.0;
}

void BL_MeshDeformer::Relink(std::map<SCA_IObject *, SCA_IObject *>& map)
{
	m_gameobj = static_cast<BL_DeformableGameObject *>(map[m_gameobj]);
}

/**
 * \warning This function is expensive!
 */
void BL_MeshDeformer::RecalcNormals()
{
	/* We don't normalize for performance, not doing it for faces normals
	 * gives area-weight normals which often look better anyway, and use
	 * GL_NORMALIZE so we don't have to do per vertex normalization either
	 * since the GPU can do it faster */

	/* set vertex normals to zero */
	for (std::array<float, 3>& normal : m_transnors) {
		normal = {{0.0f, 0.0f, 0.0f}};
	}

	for (RAS_IDisplayArray *array : m_displayArrayList) {
		for (unsigned int i = 0, size = array->GetTriangleIndexCount(); i < size; i += 3) {
			const float *co[3];
			bool flat = false;

			for (unsigned short j = 0; j < 3; ++j) {
				const unsigned int index = array->GetTriangleIndex(i + j);
				const RAS_VertexInfo& vinfo = array->GetVertexInfo(index);
				const unsigned int origindex = vinfo.getOrigIndex();

				co[j] = m_transverts[origindex].data();
				flat |= (vinfo.getFlag() & RAS_VertexInfo::FLAT);
			}

			float pnorm[3];
			normal_tri_v3(pnorm, co[0], co[1], co[2]);

			for (unsigned short j = 0; j < 3; ++j) {
				const unsigned int index = array->GetTriangleIndex(i + j);

				if (flat) {
					RAS_IVertex *vert = array->GetVertex(index);
					vert->SetNormal(pnorm);
				}
				else {
					const RAS_VertexInfo& vinfo = array->GetVertexInfo(index);
					const unsigned int origindex = vinfo.getOrigIndex();
					add_v3_v3(m_transnors[origindex].data(), pnorm);
				}
			}
		}
	}

	// Assign smooth vertex normals.
	for (RAS_IDisplayArray *array: m_displayArrayList) {
		for (unsigned int i = 0, size = array->GetVertexCount(); i < size; ++i) {
			RAS_IVertex *v = array->GetVertex(i);
			const RAS_VertexInfo& vinfo = array->GetVertexInfo(i);

			if (!(vinfo.getFlag() & RAS_VertexInfo::FLAT)) {
				v->SetNormal(m_transnors[vinfo.getOrigIndex()].data());
			}
		}
	}
}

void BL_MeshDeformer::VerifyStorage()
{
	/* Ensure that we have the right number of verts assigned */
	const unsigned int totvert = m_bmesh->totvert;
	if (m_transverts.size() != totvert) {
		m_transverts.resize(totvert);
		m_transnors.resize(totvert);
	}

	for (unsigned int v = 0; v < totvert; ++v) {
		copy_v3_v3(m_transverts[v].data(), m_bmesh->mvert[v].co);
		normal_short_to_float_v3(m_transnors[v].data(), m_bmesh->mvert[v].no);
	}
}

