/*
 * gsttcpraop.h: TCP depayloader for RAOP
 *
 * Copyright (C) 2016 Alexandre Dilly <dillya@sparod.com>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#ifndef __GST_TCP_RAOP_H__
#define __GST_TCP_RAOP_H__

#include <gst/base/gstbaseparse.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_TCP_RAOP (gst_tcp_raop_get_type ())
#define GST_TCP_RAOP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_TCP_RAOP, GstTcpRaop))
#define GST_TCP_RAOP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_TCP_RAOP, GstTcpRaopClass))
#define GST_TCP_RAOP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_TCP_RAOP, GstTcpRaopClass))
#define GST_IS_TCP_RAOP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_TCP_RAOP))
#define GST_IS_TCP_RAOP_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_TCP_RAOP))

typedef struct _GstTcpRaop GstTcpRaop;
typedef struct _GstTcpRaopClass GstTcpRaopClass;
typedef struct _GstTcpRaopPrivate GstTcpRaopPrivate;

struct _GstTcpRaop {
  GstBaseParse parent;

  /*< private >*/
  GstTcpRaopPrivate *priv;
};

struct _GstTcpRaopClass {
  GstBaseParseClass parent_class;
};

GType gst_tcp_raop_get_type (void);
gboolean gst_tcp_raop_plugin_init (GstPlugin *plugin);

G_END_DECLS

#endif /* __GST_TCP_RAOP_H__ */
