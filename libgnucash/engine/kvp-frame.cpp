/********************************************************************
 * kvp_frame.cpp -- Implements a key-value frame system             *
 * Copyright (C) 2000 Bill Gribble                                  *
 * Copyright (C) 2001,2003 Linas Vepstas <linas@linas.org>          *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
 *                                                                  *
 ********************************************************************/
#include <config.h>
#include "qof.h"
#include <glib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <cstdint>

#include "kvp-value.hpp"
#include "kvp-frame.hpp"
#include <typeinfo>
#include <sstream>
#include <algorithm>
#include <vector>
#include <numeric>

/* This static indicates the debugging module that this .o belongs to.  */
static QofLogModule log_module = "qof.kvp";

KvpFrameImpl::KvpFrameImpl(const KvpFrameImpl & rhs) noexcept
{
    std::for_each(rhs.m_valuemap.begin(), rhs.m_valuemap.end(),
        [this](const map_type::value_type & a)
        {
            auto key = qof_string_cache_insert(a.first);
            auto val = new KvpValueImpl(*a.second);
            this->m_valuemap.insert({key,val});
        }
    );
}

KvpFrameImpl::~KvpFrameImpl() noexcept
{
    std::for_each(m_valuemap.begin(), m_valuemap.end(),
		 [](const map_type::value_type &a){
		      qof_string_cache_remove(a.first);
		      delete a.second;
		  }
	);
    m_valuemap.clear();
}

KvpFrame *
KvpFrame::get_child_frame_or_nullptr (Path const & path) noexcept
{
    if (!path.size ())
        return this;
    auto key = path.front ();
    auto map_iter = m_valuemap.find (key.c_str ());
    if (map_iter == m_valuemap.end ())
        return nullptr;
    auto child = map_iter->second->get <KvpFrame *> ();
    if (!child)
        return nullptr;
    Path send;
    std::copy (path.begin () + 1, path.end (), std::back_inserter (send));
    return child->get_child_frame_or_nullptr (send);
}

KvpFrame *
KvpFrame::get_child_frame_or_create (Path const & path) noexcept
{
    if (!path.size ())
        return this;
    auto key = path.front ();
    auto spot = m_valuemap.find (key.c_str ());
    if (spot == m_valuemap.end () || spot->second->get_type () != KvpValue::Type::FRAME)
        delete set_impl (key.c_str (), new KvpValue {new KvpFrame});
    Path send;
    std::copy (path.begin () + 1, path.end (), std::back_inserter (send));
    auto child_val = m_valuemap.at (key.c_str ());
    auto child = child_val->get <KvpFrame *> ();
    return child->get_child_frame_or_create (send);
}


KvpValue *
KvpFrame::set_impl (std::string const & key, KvpValue * value) noexcept
{
    KvpValue * ret {};
    auto spot = m_valuemap.find (key.c_str ());
    if (spot != m_valuemap.end ())
    {
        qof_string_cache_remove (spot->first);
        ret = spot->second;
        m_valuemap.erase (spot);
    }
    if (value)
    {
        auto cachedkey = static_cast <char const *> (qof_string_cache_insert (key.c_str ()));
        m_valuemap.emplace (cachedkey, value);
    }
    return ret;
}

KvpValue *
KvpFrameImpl::set (Path path, KvpValue* value) noexcept
{
    if (path.empty())
        return nullptr;
    auto key = path.back ();
    path.pop_back ();
    auto target = get_child_frame_or_nullptr (path);
    if (!target)
        return nullptr;
    return target->set_impl (key, value);
}

KvpValue *
KvpFrameImpl::set_path (Path path, KvpValue* value) noexcept
{
    auto key = path.back();
    path.pop_back();
    auto target = get_child_frame_or_create (path);
    if (!target)
        return nullptr;
    return target->set_impl (key, value);
}

KvpValue *
KvpFrameImpl::get_slot (Path path) noexcept
{
    auto key = path.back();
    path.pop_back();
    auto target = get_child_frame_or_nullptr (path);
    if (!target)
        return nullptr;
    auto spot = target->m_valuemap.find (key.c_str ());
    if (spot != target->m_valuemap.end ())
        return spot->second;
    return nullptr;
}

std::string
KvpFrameImpl::to_string() const noexcept
{
    return to_string("");
}

std::string
KvpFrameImpl::to_string(std::string const & prefix) const noexcept
{
    if (!m_valuemap.size())
        return prefix;
    std::ostringstream ret;
    std::for_each(m_valuemap.begin(), m_valuemap.end(),
        [&ret,&prefix](const map_type::value_type &a)
        {
            std::string new_prefix {prefix};
            if (a.first)
            {
                new_prefix += a.first;
                new_prefix += "/";
            }
            if (a.second)
                ret << a.second->to_string(new_prefix) << "\n";
            else
                ret << new_prefix << "(null)\n";
        }
    );
    return ret.str();
}

std::vector<std::string>
KvpFrameImpl::get_keys() const noexcept
{
    std::vector<std::string> ret;
    ret.reserve (m_valuemap.size());
    std::for_each(m_valuemap.begin(), m_valuemap.end(),
        [&ret](const KvpFrameImpl::map_type::value_type &a)
        {
            ret.push_back(a.first);
        }
    );
    return ret;
}

int compare(const KvpFrameImpl * one, const KvpFrameImpl * two) noexcept
{
    if (one && !two) return 1;
    if (!one && two) return -1;
    if (!one && !two) return 0;
    return compare(*one, *two);
}

/**
 * If the first KvpFrameImpl has an item that the second does not, 1 is returned.
 * The first item within the two KvpFrameImpl that is not similar, that comparison is returned.
 * If all the items within the first KvpFrameImpl match items within the second, but the
 *   second has more elements, -1 is returned.
 * Otherwise, 0 is returned.
 */
int compare(const KvpFrameImpl & one, const KvpFrameImpl & two) noexcept
{
    for (const auto & a : one.m_valuemap)
    {
        auto otherspot = two.m_valuemap.find(a.first);
        if (otherspot == two.m_valuemap.end())
        {
            return 1;
        }
        auto comparison = compare(a.second,otherspot->second);

        if (comparison != 0)
            return comparison;
    }

    if (one.m_valuemap.size() < two.m_valuemap.size())
        return -1;
    return 0;
}

void
gvalue_from_kvp_value (const KvpValue *kval, GValue* val)
{
    if (kval == NULL) return;
    g_value_unset(val);

    switch (kval->get_type())
    {
        case KvpValue::Type::INT64:
            g_value_init (val, G_TYPE_INT64);
            g_value_set_int64 (val, kval->get<int64_t>());
            break;
        case KvpValue::Type::DOUBLE:
            g_value_init (val, G_TYPE_DOUBLE);
            g_value_set_double (val, kval->get<double>());
            break;
        case KvpValue::Type::NUMERIC:
            g_value_init (val, GNC_TYPE_NUMERIC);
            g_value_set_static_boxed (val, kval->get_ptr<gnc_numeric>());
            break;
        case KvpValue::Type::STRING:
            g_value_init (val, G_TYPE_STRING);
            g_value_set_static_string (val, kval->get<const char*>());
            break;
        case KvpValue::Type::GUID:
            g_value_init (val, GNC_TYPE_GUID);
            g_value_set_static_boxed (val, kval->get<GncGUID*>());
            break;
        case KvpValue::Type::TIME64:
            g_value_init (val, GNC_TYPE_TIME64);
            g_value_set_boxed (val, kval->get_ptr<Time64>());
            break;
        case KvpValue::Type::GDATE:
            g_value_init (val, G_TYPE_DATE);
            g_value_set_static_boxed (val, kval->get_ptr<GDate>());
            break;
        default:
/* No transfer outside of QofInstance-derived classes! */
            PWARN ("Error! Invalid attempt to transfer Kvp type %d", kval->get_type());
            break;
    }
}

KvpValue*
kvp_value_from_gvalue (const GValue *gval)
{
    KvpValue *val = NULL;
    GType type;
    if (gval == NULL)
        return NULL;
    type = G_VALUE_TYPE (gval);
    g_return_val_if_fail (G_VALUE_TYPE (gval), NULL);

    if (type == G_TYPE_INT64)
        val = new KvpValue(g_value_get_int64 (gval));
    else if (type == G_TYPE_DOUBLE)
        val = new KvpValue(g_value_get_double (gval));
    else if (type == G_TYPE_BOOLEAN)
    {
        auto bval = g_value_get_boolean(gval);
        if (bval)
            val = new KvpValue(g_strdup("true"));
    }
    else if (type == GNC_TYPE_NUMERIC)
        val = new KvpValue(*(gnc_numeric*)g_value_get_boxed (gval));
    else if (type == G_TYPE_STRING)
    {
        auto string = g_value_get_string(gval);
        if (string != nullptr)
            val = new KvpValue(g_strdup(string));
    }
    else if (type == GNC_TYPE_GUID)
    {
        auto boxed = g_value_get_boxed(gval);
        if (boxed != nullptr)
            val = new KvpValue(guid_copy(static_cast<GncGUID*>(boxed)));
    }
    else if (type == GNC_TYPE_TIME64)
        val = new KvpValue(*(Time64*)g_value_get_boxed (gval));
    else if (type == G_TYPE_DATE)
        val = new KvpValue(*(GDate*)g_value_get_boxed (gval));
    else
        PWARN ("Error! Don't know how to make a KvpValue from a %s",
               G_VALUE_TYPE_NAME (gval));

    return val;
}

void
KvpFrame::flatten_kvp_impl(std::vector <std::string> path, std::vector <KvpEntry> & entries) const noexcept
{
    for (auto const & entry : m_valuemap)
    {
        std::vector<std::string> new_path {path};
        new_path.push_back("/");
        if (entry.second->get_type() == KvpValue::Type::FRAME)
        {
            new_path.push_back(entry.first);
            entry.second->get<KvpFrame*>()->flatten_kvp_impl(new_path, entries);
        }
        else
        {
            new_path.emplace_back (entry.first);
            entries.emplace_back (new_path, entry.second);
        }
    }
}

std::vector <KvpEntry>
KvpFrame::flatten_kvp(void) const noexcept
{
    std::vector <KvpEntry> ret;
    flatten_kvp_impl({}, ret);
    return ret;
}
