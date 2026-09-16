/*
Copyright (c) 2010 - Wii Banner Player Project

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/

#include <GL/glew.h>

#include <libwb/Pane.h>
#include <libwb/Layout.h>
#include <libwb/Endian.h>
#include <libwb/Funcs.h>
#include <libwb/Material.h>

namespace WiiBanner
{

void Pane::Load(std::istream& file)
{
	hide = false;

	file >> BE >> flags >> origin >> alpha;
	file.seekg(1, std::ios::cur);

	SetName(ReadFixedLengthString<NAME_LENGTH>(file));

	ReadFixedLengthString<USER_DATA_LENGTH>(file);	// user data

	file >> BE
		>> translate.x >> translate.y >> translate.z
		>> rotate.x >> rotate.y >> rotate.z
		>> scale.x >> scale.y
		>> width >> height;
}

Pane::~Pane()
{
	// delete children
	for (Pane* pane : panes)
		delete pane;
}

void Pane::SetFrame(FrameNumber frame, uint8_t key_set)
{
	// setframe on self
	Animator::SetFrame(frame, key_set);

	// setframe on children
	for (Pane* pane : panes)
		pane->SetFrame(frame, key_set);
}

void Pane::Render(const Resources& resources, uint8_t parent_alpha, bool widescreen) const {
	if (!GetVisible() || GetHide())
		return;

	uint8_t render_alpha =
		GetInfluencedAlpha()
			? MultiplyColors(parent_alpha, GetAlpha())
			: GetAlpha();

	// kinda copied from usb loader gx
	float ws_scale = 1.f;

	if (widescreen) {
		ws_scale = 0.82f;
		widescreen = false;
	}

	glPushMatrix();

	glTranslatef(
		GetTranslate().x,
		GetTranslate().y,
		GetTranslate().z
	);

	glRotatef(GetRotate().z, 0, 0, 1);
	glRotatef(GetRotate().y, 0, 1, 0);
	glRotatef(GetRotate().x, 1, 0, 0);

	glScalef(
		GetScale().x * ws_scale,
		GetScale().y,
		1.f
	);

	Draw(resources, render_alpha);

	for (Pane* pane : panes)
		pane->Render(resources, render_alpha, widescreen);

	glPopMatrix();
}

Pane* Pane::FindPane(const std::string& find_name)
{
	if (find_name == GetName())
		return this;

	Pane* found = nullptr;

	for (Pane* pane : panes)
	{
		found = pane->FindPane(find_name);
		if (found)
			break;
	}

	return found;
}

void Pane::ProcessHermiteKey(const KeyType& type, float value)
{
	if (type.type == ANIMATION_TYPE_VERTEX_COLOR)	// vertex color
	{
		// only alpha is supported for Panes afaict
		if (0x10 == type.target)
		{
			alpha = (uint8_t)value;
			return;
		}
	}
	else if (type.type == ANIMATION_TYPE_PANE)	// pane animation
	{
		if (type.target < 10)
		{
			float* const values[] =
			{
				&translate.x,
				&translate.y,
				&translate.z,

				&rotate.x,
				&rotate.y,
				&rotate.z,

				&scale.x,
				&scale.y,

				&width,
				&height,
			};

			*values[type.target] = value;
			return;
		}
	}

	Base::ProcessHermiteKey(type, value);
}

void Pane::ProcessStepKey(const KeyType& type, StepKeyHandler::KeyData data)
{
	if (type.type == ANIMATION_TYPE_VISIBILITY)	// visibility
	{
		SetVisible(!!data.data2);
		return;
	}

	Base::ProcessStepKey(type, data);
}

void Quad::Load(std::istream& file)
{
	ReadBEArray(file, &vertex_colors->r, sizeof(vertex_colors));

	uint8_t tex_coord_count;

	file >> BE >> material_index >> tex_coord_count;
	file.seekg(1, std::ios::cur);

	tex_coords.resize(tex_coord_count);
	if (tex_coord_count)
		ReadBEArray(file, &tex_coords[0].coords->s, sizeof(TexCoords) / sizeof(float) * tex_coord_count);
}

	void Quad::Draw(const Resources& resources, uint8_t render_alpha) const
{
	const Material* material = nullptr;

	if (material_index < resources.materials.size())
	{
		material = resources.materials[material_index];
		material->Apply(resources);
	}

	auto quad_vertex = [=](unsigned int v, float x, float y)
	{
		const GXColor draw_color = material
			? material->GetChannelColor(vertex_colors[v], render_alpha)
			: GXColor{
				vertex_colors[v].r,
				vertex_colors[v].g,
				vertex_colors[v].b,
				MultiplyColors(vertex_colors[v].a, render_alpha)
			};

		glColor4ub(draw_color.r, draw_color.g, draw_color.b, draw_color.a);

		if (tex_coords.size() == 1)
		{
			auto* fv = &tex_coords.front().coords[v].s;
			for (GLenum t = GL_TEXTURE0; t != GL_TEXTURE8; ++t)
				glMultiTexCoord2fv(t, fv);
		}
		else
		{
			GLenum target = GL_TEXTURE0;
			for (auto& tc : tex_coords)
				glMultiTexCoord2fv(target++, &tc.coords[v].s);
		}

		glVertex2f(x, y);
	};

	glPushMatrix();

	glScalef(
		GetWidth(),
		GetHeight(),
		1.f
	);

	glTranslatef(
		-0.5f * GetOriginX(),
		-0.5f * GetOriginY(),
		1.f
	);

	glBegin(GL_QUADS);

	quad_vertex(2, 0.f, 0.f);
	quad_vertex(3, 1.f, 0.f);
	quad_vertex(1, 1.f, 1.f);
	quad_vertex(0, 0.f, 1.f);

	glEnd();

	glPopMatrix();
}

void Quad::ProcessHermiteKey(const KeyType& type, float value)
{
	if (type.type == ANIMATION_TYPE_VERTEX_COLOR)	// vertex color
	{
		if (type.target < 0x10)
		{
			// vertex colors
			(&vertex_colors->r)[type.target] = (uint8_t)value;
			return;
		}
	}

	Base::ProcessHermiteKey(type, value);
}

}
