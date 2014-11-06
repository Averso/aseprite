/* Aseprite
 * Copyright (C) 2001-2013  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "raster/layer.h"

#include "raster/blend.h"
#include "raster/cel.h"
#include "raster/image.h"
#include "raster/primitives.h"
#include "raster/sprite.h"
#include "raster/stock.h"

#include <algorithm>
#include <string.h>

namespace raster {

Layer::Layer(ObjectType type, Sprite* sprite)
  : Object(type)
{
  ASSERT(type == OBJECT_LAYER_IMAGE || type == OBJECT_LAYER_FOLDER);

  setName("Layer");

  m_sprite = sprite;
  m_parent = NULL;
  m_flags =
    LAYER_IS_READABLE |
    LAYER_IS_WRITABLE;
}

Layer::~Layer()
{
}

int Layer::getMemSize() const
{
  return sizeof(Layer);
}

Layer* Layer::getPrevious() const
{
  if (m_parent != NULL) {
    LayerConstIterator it =
      std::find(m_parent->getLayerBegin(),
                m_parent->getLayerEnd(), this);

    if (it != m_parent->getLayerEnd() &&
        it != m_parent->getLayerBegin()) {
      it--;
      return *it;
    }
  }
  return NULL;
}

Layer* Layer::getNext() const
{
  if (m_parent != NULL) {
    LayerConstIterator it =
      std::find(m_parent->getLayerBegin(),
                m_parent->getLayerEnd(), this);

    if (it != m_parent->getLayerEnd()) {
      it++;
      if (it != m_parent->getLayerEnd())
        return *it;
    }
  }
  return NULL;
}

//////////////////////////////////////////////////////////////////////
// LayerImage class

LayerImage::LayerImage(Sprite* sprite)
  : Layer(OBJECT_LAYER_IMAGE, sprite)
{
}

LayerImage::~LayerImage()
{
  destroyAllCels();
}

int LayerImage::getMemSize() const
{
  int size = sizeof(LayerImage);
  CelConstIterator it = getCelBegin();
  CelConstIterator end = getCelEnd();

  for (; it != end; ++it) {
    const Cel* cel = *it;
    size += cel->getMemSize();

    const Image* image = cel->image();
    size += image->getMemSize();
  }

  return size;
}

void LayerImage::destroyAllCels()
{
  CelIterator it = getCelBegin();
  CelIterator end = getCelEnd();

  for (; it != end; ++it) {
    Cel* cel = *it;
    Image* image = cel->image();

    ASSERT(image != NULL);

    sprite()->stock()->removeImage(image);
    delete image;
    delete cel;
  }
  m_cels.clear();
}

void LayerImage::getCels(CelList& cels) const
{
  CelConstIterator it = getCelBegin();
  CelConstIterator end = getCelEnd();

  for (; it != end; ++it)
    cels.push_back(*it);
}

Cel* LayerImage::getLastCel() const
{
  if (!m_cels.empty())
    return m_cels.back();
  else
    return NULL;
}

void LayerImage::addCel(Cel *cel)
{
  CelIterator it = getCelBegin();
  CelIterator end = getCelEnd();

  for (; it != end; ++it) {
    if ((*it)->frame() > cel->frame())
      break;
  }

  m_cels.insert(it, cel);

  cel->setParentLayer(this);
}

/**
 * Removes the cel from the layer.
 *
 * It doesn't destroy the cel, you have to delete it after calling
 * this routine.
 */
void LayerImage::removeCel(Cel *cel)
{
  CelIterator it = std::find(m_cels.begin(), m_cels.end(), cel);

  ASSERT(it != m_cels.end());

  m_cels.erase(it);
}

void LayerImage::moveCel(Cel* cel, FrameNumber frame)
{
  removeCel(cel);
  cel->setFrame(frame);
  addCel(cel);
}

const Cel* LayerImage::getCel(FrameNumber frame) const
{
  CelConstIterator it = getCelBegin();
  CelConstIterator end = getCelEnd();

  for (; it != end; ++it) {
    const Cel* cel = *it;
    if (cel->frame() == frame)
      return cel;
  }

  return NULL;
}

Cel* LayerImage::getCel(FrameNumber frame)
{
  return const_cast<Cel*>(static_cast<const LayerImage*>(this)->getCel(frame));
}

/**
 * Configures some properties of the specified layer to make it as the
 * "Background" of the sprite.
 *
 * You can't use this routine if the sprite already has a background
 * layer.
 */
void LayerImage::configureAsBackground()
{
  ASSERT(sprite() != NULL);
  ASSERT(sprite()->backgroundLayer() == NULL);

  setMoveable(false);
  setBackground(true);
  setName("Background");

  sprite()->folder()->stackLayer(this, NULL);
}

//////////////////////////////////////////////////////////////////////
// LayerFolder class

LayerFolder::LayerFolder(Sprite* sprite)
  : Layer(OBJECT_LAYER_FOLDER, sprite)
{
  setName("Layer Set");
}

LayerFolder::~LayerFolder()
{
  destroyAllLayers();
}

void LayerFolder::destroyAllLayers()
{
  LayerIterator it = getLayerBegin();
  LayerIterator end = getLayerEnd();

  for (; it != end; ++it) {
    Layer* layer = *it;
    delete layer;
  }
  m_layers.clear();
}

int LayerFolder::getMemSize() const
{
  int size = sizeof(LayerFolder);
  LayerConstIterator it = getLayerBegin();
  LayerConstIterator end = getLayerEnd();

  for (; it != end; ++it) {
    const Layer* layer = *it;
    size += layer->getMemSize();
  }

  return size;
}

void LayerFolder::getCels(CelList& cels) const
{
  LayerConstIterator it = getLayerBegin();
  LayerConstIterator end = getLayerEnd();

  for (; it != end; ++it)
    (*it)->getCels(cels);
}

void LayerFolder::addLayer(Layer* layer)
{
  m_layers.push_back(layer);
  layer->setParent(this);
}

void LayerFolder::removeLayer(Layer* layer)
{
  LayerIterator it = std::find(m_layers.begin(), m_layers.end(), layer);
  ASSERT(it != m_layers.end());
  m_layers.erase(it);

  layer->setParent(NULL);
}

void LayerFolder::stackLayer(Layer* layer, Layer* after)
{
  ASSERT(layer != after);
  if (layer == after)
    return;

  LayerIterator it = std::find(m_layers.begin(), m_layers.end(), layer);
  ASSERT(it != m_layers.end());
  m_layers.erase(it);

  if (after) {
    LayerIterator after_it = std::find(m_layers.begin(), m_layers.end(), after);
    ASSERT(after_it != m_layers.end());
    after_it++;
    m_layers.insert(after_it, layer);
  }
  else
    m_layers.push_front(layer);
}

void layer_render(const Layer* layer, Image* image, int x, int y, FrameNumber frame)
{
  if (!layer->isReadable())
    return;

  switch (layer->type()) {

    case OBJECT_LAYER_IMAGE: {
      const Cel* cel = static_cast<const LayerImage*>(layer)->getCel(frame);
      Image* src_image;

      if (cel) {
        ASSERT((cel->imageIndex() >= 0) &&
               (cel->imageIndex() < layer->sprite()->stock()->size()));

        src_image = cel->image();
        ASSERT(src_image != NULL);

        ASSERT(src_image->maskColor() == layer->sprite()->transparentColor());

        composite_image(image, src_image,
          cel->x() + x,
          cel->y() + y,
          MID(0, cel->opacity(), 255),
          static_cast<const LayerImage*>(layer)->getBlendMode());
      }
      break;
    }

    case OBJECT_LAYER_FOLDER: {
      LayerConstIterator it = static_cast<const LayerFolder*>(layer)->getLayerBegin();
      LayerConstIterator end = static_cast<const LayerFolder*>(layer)->getLayerEnd();

      for (; it != end; ++it)
        layer_render(*it, image, x, y, frame);

      break;
    }

  }
}

} // namespace raster