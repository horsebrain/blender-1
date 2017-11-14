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

/** \file RAS_DisplayArrayBucket.h
 *  \ingroup bgerast
 */

#ifndef __RAS_DISPLAY_MATERIAL_BUCKET_H__
#define __RAS_DISPLAY_MATERIAL_BUCKET_H__

#include "CM_Update.h"

#include "RAS_MeshSlot.h"
#include "RAS_AttributeArray.h"

#include "MT_Transform.h"

#include <vector>

class RAS_MaterialBucket;
class RAS_IDisplayArray;
class RAS_MeshObject;
class RAS_MeshMaterial;
class RAS_Deformer;
class RAS_IStorageInfo;
class RAS_InstancingBuffer;

class RAS_DisplayArrayBucket
{
private:
	/// The parent bucket.
	RAS_MaterialBucket *m_bucket;
	/// The display array = list of vertexes and indexes.
	RAS_IDisplayArray *m_displayArray;
	/// The parent mesh object, it can be nullptr for text objects.
	RAS_MeshObject *m_mesh;
	/// The material mesh.
	RAS_MeshMaterial *m_meshMaterial;
	/// The list of all visible mesh slots to render this frame.
	RAS_MeshSlotList m_activeMeshSlots;
	/// The deformer using this display array.
	RAS_Deformer *m_deformer;

	RAS_DisplayArrayStorage *m_arrayStorage;
	/// Attribute array used for each different render categories.
	RAS_AttributeArray m_attribArray;

	/// The vertex buffer object containing all the data used for the instancing rendering.
	std::unique_ptr<RAS_InstancingBuffer> m_instancingBuffer;

	CM_UpdateClient<RAS_IPolyMaterial> m_materialUpdateClient;

	RAS_DisplayArrayNodeData m_nodeData;
	RAS_DisplayArrayDownwardNode m_downwardNode;
	RAS_DisplayArrayUpwardNode m_upwardNode;

	RAS_DisplayArrayDownwardNode m_instancingNode;
	RAS_DisplayArrayDownwardNode m_batchingNode;

public:
	RAS_DisplayArrayBucket(RAS_MaterialBucket *bucket, RAS_IDisplayArray *array,
						   RAS_MeshObject *mesh, RAS_MeshMaterial *meshmat, RAS_Deformer *deformer);
	~RAS_DisplayArrayBucket();

	/// \section Accesor
	RAS_MaterialBucket *GetBucket() const;
	RAS_IDisplayArray *GetDisplayArray() const;
	RAS_MeshObject *GetMesh() const;
	RAS_MeshMaterial *GetMeshMaterial() const;

	/// \section Active Mesh Slots Management.
	void ActivateMesh(RAS_MeshSlot *slot);
	/// Remove all mesh slots from the list.
	void RemoveActiveMeshSlots();

	/// \section Render Infos
	bool UseBatching() const;

	/// Update render infos.
	void UpdateActiveMeshSlots(RAS_Rasterizer::DrawType drawingMode);

	void GenerateTree(RAS_MaterialDownwardNode& downwardRoot, RAS_MaterialUpwardNode& upwardRoot,
			RAS_UpwardTreeLeafs& upwardLeafs, RAS_Rasterizer::DrawType drawingMode, bool sort, bool instancing);
	void BindUpwardNode(const RAS_DisplayArrayNodeTuple& tuple);
	void UnbindUpwardNode(const RAS_DisplayArrayNodeTuple& tuple);
	void RunDownwardNode(const RAS_DisplayArrayNodeTuple& tuple);
	void RunDownwardNodeNoArray(const RAS_DisplayArrayNodeTuple& tuple);
	void RunInstancingNode(const RAS_DisplayArrayNodeTuple& tuple);
	void RunBatchingNode(const RAS_DisplayArrayNodeTuple& tuple);

	/// Replace the material bucket of this display array bucket by the one given.
	void ChangeMaterialBucket(RAS_MaterialBucket *bucket);
};

typedef std::vector<RAS_DisplayArrayBucket *> RAS_DisplayArrayBucketList;

#endif  // __RAS_DISPLAY_MATERIAL_BUCKET_H__
