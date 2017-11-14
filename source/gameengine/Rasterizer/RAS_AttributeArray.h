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
 * Contributor(s): Tristan Porteries.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file RAS_AttributeArray.h
 *  \ingroup bgerast
 */

#ifndef __RAS_ATTRIBUTE_ARRAY_H__
#define __RAS_ATTRIBUTE_ARRAY_H__

#include "RAS_Rasterizer.h"

#include <vector>

class RAS_AttributeArrayStorage;
class RAS_DisplayArrayStorage;

class RAS_AttributeArray
{
public:
	enum AttribType {
		RAS_ATTRIB_INVALID = -1,
		RAS_ATTRIB_POS, // Vertex coordinates.
		RAS_ATTRIB_UV, // UV coordinates.
		RAS_ATTRIB_NORM, // Normal coordinates.
		RAS_ATTRIB_TANGENT, // Tangent coordinates.
		RAS_ATTRIB_COLOR, // Vertex color.
		RAS_ATTRIB_MAX
	};

	struct Attrib
	{
		unsigned short m_loc;
		AttribType m_type;
		bool m_texco;
		unsigned short m_layer;
	};

	/* Attribute list of the following format:
	 * hashed name: (attrib type, layer(optional)).
	 */
	using AttribList = std::vector<Attrib>;
	static const AttribList InvalidAttribList;

private:
	std::unique_ptr<RAS_AttributeArrayStorage> m_storages[RAS_Rasterizer::RAS_DRAW_MAX];
	AttribList m_attribs;
	RAS_IDisplayArray *m_array;

public:
	RAS_AttributeArray(const AttribList& attribs, RAS_IDisplayArray *array);
	~RAS_AttributeArray();

	RAS_AttributeArrayStorage *GetStorage(RAS_Rasterizer::DrawType drawingMode);
	void DestructStorages();
};

inline bool operator==(const RAS_AttributeArray::Attrib& first, const RAS_AttributeArray::Attrib& second)
{
	return first.m_loc == second.m_loc && first.m_type == second.m_type &&
			first.m_texco == second.m_texco && first.m_layer == second.m_layer;
}

#endif  // __RAS_ATTRIBUTE_ARRAY_H__
