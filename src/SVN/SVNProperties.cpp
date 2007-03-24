// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2006 - Stefan Kueng

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "stdafx.h"
#include "..\\TortoiseShell\\resource.h"
#include "registry.h"  // Just provides stdstring def
#include "SVNProperties.h"
#include "SVNStatus.h"
#include "SVNHelpers.h"

#ifdef _MFC_VER
#	include "SVN.h"
#	include "UnicodeUtils.h"
#	include "registry.h"
#	include "PathUtils.h"
#else
#include "registry.h"
extern	HINSTANCE			g_hResInst;
#endif


struct log_msg_baton3
{
	const char *message;  /* the message. */
	const char *message_encoding; /* the locale/encoding of the message. */
	const char *base_dir; /* the base directory for an external edit. UTF-8! */
	const char *tmpfile_left; /* the tmpfile left by an external edit. UTF-8! */
	apr_pool_t *pool; /* a pool. */
};

svn_error_t* svn_get_log_message(const char **log_msg,
								 const char **tmp_file,
								 const apr_array_header_t * /*commit_items*/,
								 void *baton, 
								 apr_pool_t * pool)
{
	log_msg_baton3 *lmb = (log_msg_baton3 *) baton;
	*tmp_file = NULL;
	if (lmb->message)
	{
		*log_msg = apr_pstrdup (pool, lmb->message);
	}

	return SVN_NO_ERROR;
}

svn_error_t*	SVNProperties::Refresh()
{
	svn_opt_revision_t			rev;
	m_error = NULL;

	m_propCount = 0;
#ifdef _MFC_VER
	rev.kind = ((svn_opt_revision_t *)m_rev)->kind;
	rev.value = ((svn_opt_revision_t *)m_rev)->value;
#else
	rev.kind = svn_opt_revision_unspecified;
	rev.value.number = -1;
#endif
	m_error = svn_client_proplist3 (m_path.GetSVNApiPath(),
								&rev,
								&rev,
								false,	//recurse
								proplist_receiver,
								this,
								&m_ctx,
								m_pool);
	if(m_error != NULL)
		return m_error;


	for (std::map<std::string, apr_hash_t *>::iterator it = m_props.begin(); it != m_props.end(); ++it)
	{
		m_propCount += apr_hash_count(it->second);
	}
	return NULL;
}

#ifdef _MFC_VER

SVNProperties::SVNProperties(const CTSVNPath& filepath, SVNRev rev)
: m_rev(SVNRev::REV_WC)
{
	m_rev = rev;
#else

SVNProperties::SVNProperties(const CTSVNPath& filepath)
{
#endif
	m_error = NULL;
	m_pool = svn_pool_create (NULL);				// create the memory pool

	memset (&m_ctx, 0, sizeof (m_ctx));

#ifdef _MFC_VER
	svn_config_ensure(NULL, m_pool);
	// set up the configuration
	if (svn_config_get_config (&(m_ctx.config), g_pConfigDir, m_pool))
	{
		::MessageBox(NULL, this->GetLastErrorMsg().c_str(), _T("TortoiseSVN"), MB_ICONERROR);
		svn_pool_destroy (m_pool);					// free the allocated memory
		return;
	}
#endif
	
	m_path = filepath;
#ifdef _MFC_VER
	m_prompt.Init(m_pool, &m_ctx);

	m_ctx.log_msg_func3 = svn_get_log_message;

	m_path = filepath;

	// set up the SVN_SSH param
	CString tsvn_ssh = CRegString(_T("Software\\TortoiseSVN\\SSH"));
	if (tsvn_ssh.IsEmpty())
		tsvn_ssh = CPathUtils::GetAppDirectory() + _T("TortoisePlink.exe");
	tsvn_ssh.Replace('\\', '/');
	if (!tsvn_ssh.IsEmpty())
	{
		svn_config_t * cfg = (svn_config_t *)apr_hash_get (m_ctx.config, SVN_CONFIG_CATEGORY_CONFIG,
			APR_HASH_KEY_STRING);
		svn_config_set(cfg, SVN_CONFIG_SECTION_TUNNELS, "ssh", CUnicodeUtils::GetUTF8(tsvn_ssh));
	}
#endif

	SVNProperties::Refresh();
}

SVNProperties::~SVNProperties(void)
{
	svn_pool_destroy (m_pool);					// free the allocated memory
}

int SVNProperties::GetCount()
{
	return m_propCount;
}

std::string SVNProperties::GetItem(int index, BOOL name)
{
	const void *key;
	void *val;
	svn_string_t *propval = NULL;
	const char *pname_utf8 = "";
	m_error = NULL;

	if (m_props.size() == 0)
	{
		return "";
	}
	if (index >= m_propCount)
	{
		return "";
	}

	long ind = 0;

	for (std::map<std::string, apr_hash_t *>::iterator it = m_props.begin(); it != m_props.end(); ++it)
	{
		apr_hash_index_t *hi;

		for (hi = apr_hash_first(m_pool, it->second); hi; hi = apr_hash_next(hi))
		{
			if (ind++ != index)
				continue;

			apr_hash_this(hi, &key, NULL, &val);
			propval = (svn_string_t *)val;
			pname_utf8 = (char *)key;

			// If this is a special Subversion property, it is stored as
			// UTF8, so convert to the native format.
			if ((svn_prop_needs_translation (pname_utf8))||(strncmp(pname_utf8, "bugtraq:", 8)==0)||(strncmp(pname_utf8, "tsvn:", 5)==0))
			{
				m_error = svn_subst_detranslate_string (&propval, propval, FALSE, m_pool);
				if (m_error != NULL)
					return "";
			}
		}
	}

	if (name)
		return pname_utf8;
	else if (propval)
		return std::string(propval->data, propval->len);
	else
		return "";
}

BOOL SVNProperties::IsSVNProperty(int index)
{
	const char *pname_utf8;
	const char *name = SVNProperties::GetItem(index, true).c_str();

	svn_utf_cstring_to_utf8 (&pname_utf8, name, m_pool);
	svn_boolean_t is_svn_prop = svn_prop_needs_translation (pname_utf8);

	return is_svn_prop;
}

bool SVNProperties::IsBinary(int index)
{
	std::string value = GetItem(index, false);
	return IsBinary(value);
}

bool SVNProperties::IsBinary(std::string value)
{
	const char * pvalue = (const char *)value.c_str();
	// check if there are any null bytes in the string
	for (size_t i=0; i<value.size(); ++i)
	{
		if (*pvalue == '\0')
		{
			// if there are only null bytes until the end of the string,
			// we still treat it as not binary
			while (i<value.size())
			{
				if (*pvalue != '\0')
					return true;
				++i;
				pvalue++;
			}
			return false;
		}
		pvalue++;
	}
	return false;
}

stdstring SVNProperties::GetItemName(int index)
{
	return UTF8ToString(SVNProperties::GetItem(index, true));
}

std::string SVNProperties::GetItemValue(int index)
{
	return SVNProperties::GetItem(index, false);
}

BOOL SVNProperties::Add(const TCHAR * Name, std::string Value, BOOL recurse, const TCHAR * message)
{
	svn_string_t*	pval;
	std::string		pname_utf8;
	m_error = NULL;

	SVNPool subpool(m_pool);

	pval = svn_string_ncreate (Value.c_str(), Value.size(), subpool);

	pname_utf8 = StringToUTF8(Name);
	if ((svn_prop_needs_translation (pname_utf8.c_str()))||(strncmp(pname_utf8.c_str(), "bugtraq:", 8)==0)||(strncmp(pname_utf8.c_str(), "tsvn:", 5)==0))
	{
		m_error = svn_subst_translate_string (&pval, pval, NULL, subpool);
		if (m_error != NULL)
			return FALSE;
	}
	if ((!m_path.IsDirectory())&&(((strncmp(pname_utf8.c_str(), "bugtraq:", 8)==0)||(strncmp(pname_utf8.c_str(), "tsvn:", 5)==0))))
	{
		// bugtraq: and tsvn: properties are not allowed on files.
#ifdef _MFC_VER
		CString temp;
		temp.LoadString(IDS_ERR_PROPNOTONFILE);
		CStringA tempA = CStringA(temp);
		m_error = svn_error_create(NULL, NULL, tempA);
#else
		TCHAR string[1024];
		LoadStringEx(g_hResInst, IDS_ERR_PROPNOTONFILE, string, 1024, (WORD)CRegStdWORD(_T("Software\\TortoiseSVN\\LanguageID"), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)));
		std::string stringA = WideToMultibyte(wide_string(string));
		m_error = svn_error_create(NULL, NULL, stringA.c_str());
#endif
		return FALSE;
	}
	if (strncmp(pname_utf8.c_str(), "bugtraq:message", 15)==0)
	{
		// make sure that the bugtraq:message property only contains one single line!
		for (apr_size_t i=0; i<pval->len; ++i)
		{
			if ((pval->data[i] == '\n')||(pval->data[0] == '\r'))
			{
#ifdef _MFC_VER
				CString temp;
				temp.LoadString(IDS_ERR_PROPNOMULTILINE);
				CStringA tempA = CStringA(temp);
				m_error = svn_error_create(NULL, NULL, tempA);
#else
				TCHAR string[1024];
				LoadStringEx(g_hResInst, IDS_ERR_PROPNOMULTILINE, string, 1024, (WORD)CRegStdWORD(_T("Software\\TortoiseSVN\\LanguageID"), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)));
				std::string stringA = WideToMultibyte(wide_string(string));
				m_error = svn_error_create(NULL, NULL, stringA.c_str());
#endif
				return FALSE;
			}
		}
	}
	if ((recurse)&&((strncmp(pname_utf8.c_str(), "bugtraq:", 8)==0)||(strncmp(pname_utf8.c_str(), "tsvn:", 5)==0)))
	{
		// The bugtraq and tsvn properties must only be set on folders.
		CTSVNPath path;
		SVNStatus stat;
		svn_wc_status2_t * status = NULL;
		status = stat.GetFirstFileStatus(m_path, path, false, svn_depth_infinity, true, true);
		do 
		{
			if (status)
			{
				if ((status->entry)&&(status->entry->kind == svn_node_dir))
				{
					// a versioned folder, so set the property!
					svn_commit_info_t *commit_info = svn_create_commit_info(subpool);
					m_error = svn_client_propset3 (&commit_info, pname_utf8.c_str(), pval, path.GetSVNApiPath(), false, false, m_rev, &m_ctx, subpool);
				}
			}
			status = stat.GetNextFileStatus(path);
		} while ((status != 0)&&(m_error == NULL));
	}
	else 
	{
		svn_commit_info_t *commit_info = svn_create_commit_info(subpool);
		if (m_path.IsUrl())
		{
			CString msg = message ? message : _T("");
			msg.Replace(_T("\r"), _T(""));
			log_msg_baton3* baton = (log_msg_baton3 *) apr_palloc (subpool, sizeof (*baton));
			baton->message = apr_pstrdup(subpool, CUnicodeUtils::GetUTF8(msg));
			baton->base_dir = "";
			baton->message_encoding = NULL;
			baton->tmpfile_left = NULL;
			baton->pool = subpool;
			m_ctx.log_msg_baton3 = baton;
		}
		m_error = svn_client_propset3 (&commit_info, pname_utf8.c_str(), pval, m_path.GetSVNApiPath(), recurse, false, m_rev, &m_ctx, subpool);
	}
	if (m_error != NULL)
	{
		return FALSE;
	}

	// rebuild the property list
	m_error = SVNProperties::Refresh();
	if (m_error != NULL)
	{
		return FALSE;
	}
	return TRUE;
}

BOOL SVNProperties::Remove(const TCHAR * Name, BOOL recurse, const TCHAR * message)
{
	std::string		pname_utf8;
	m_error = NULL;

	SVNPool subpool(m_pool);

	pname_utf8 = StringToUTF8(Name);

	svn_commit_info_t *commit_info = svn_create_commit_info(subpool);
	if (m_path.IsUrl())
	{
		CString msg = message ? message : _T("");
		msg.Replace(_T("\r"), _T(""));
		log_msg_baton3* baton = (log_msg_baton3 *) apr_palloc (subpool, sizeof (*baton));
		baton->message = apr_pstrdup(subpool, CUnicodeUtils::GetUTF8(msg));
		baton->base_dir = "";
		baton->message_encoding = NULL;
		baton->tmpfile_left = NULL;
		baton->pool = subpool;
		m_ctx.log_msg_baton3 = baton;
	}

	m_error = svn_client_propset3 (&commit_info, pname_utf8.c_str(), NULL, m_path.GetSVNApiPath(), recurse, false, m_rev, &m_ctx, subpool);

	if (m_error != NULL)
	{
		return FALSE;
	}

	// rebuild the property list
	m_error = Refresh();
	if (m_error != NULL)
	{
		return FALSE;
	}
	return TRUE;
}

stdstring SVNProperties::GetLastErrorMsg()
{
	stdstring msg;
	char errbuf[256];

	if (m_error != NULL)
	{
		svn_error_t * ErrPtr = m_error;
		if (ErrPtr->message)
			msg = UTF8ToString(ErrPtr->message);
		else
		{
			/* Is this a Subversion-specific error code? */
			if ((ErrPtr->apr_err > APR_OS_START_USEERR)
				&& (ErrPtr->apr_err <= APR_OS_START_CANONERR))
				msg = UTF8ToString(svn_strerror (ErrPtr->apr_err, errbuf, sizeof (errbuf)));
			/* Otherwise, this must be an APR error code. */
			else
			{
				svn_error_t *temp_err = NULL;
				const char * err_string = NULL;
				temp_err = svn_utf_cstring_to_utf8(&err_string, apr_strerror (ErrPtr->apr_err, errbuf, sizeof (errbuf)-1), ErrPtr->pool);
				if (temp_err)
				{
					svn_error_clear (temp_err);
					msg = _T("Can't recode error string from APR");
				}
				else
				{
					msg = UTF8ToString(err_string);
				}
			}
		}
		while (ErrPtr->child)
		{
			ErrPtr = ErrPtr->child;
			if (ErrPtr->message)
			{
				msg += _T("\n");
				if (ErrPtr->message)
					msg += UTF8ToString(ErrPtr->message);
				else
				{
					/* Is this a Subversion-specific error code? */
					if ((ErrPtr->apr_err > APR_OS_START_USEERR)
						&& (ErrPtr->apr_err <= APR_OS_START_CANONERR))
						msg += UTF8ToString(svn_strerror (ErrPtr->apr_err, errbuf, sizeof (errbuf)));
					/* Otherwise, this must be an APR error code. */
					else
					{
						svn_error_t *temp_err = NULL;
						const char * err_string = NULL;
						temp_err = svn_utf_cstring_to_utf8(&err_string, apr_strerror (ErrPtr->apr_err, errbuf, sizeof (errbuf)-1), ErrPtr->pool);
						if (temp_err)
						{
							svn_error_clear (temp_err);
							msg += _T("Can't recode error string from APR");
						}
						else
						{
							msg += UTF8ToString(err_string);
						}
					}
				}
			}
		} 
		return msg;
	} 
	return msg;
}

svn_error_t * SVNProperties::proplist_receiver(void *baton, svn_stringbuf_t *path, apr_hash_t *prop_hash, apr_pool_t *pool)
{
	SVNProperties * pThis = (SVNProperties*)baton;
	if (pThis)
	{
		svn_error_t * error;
		const char *node_name_native;
		error = svn_utf_cstring_from_utf8_stringbuf (&node_name_native, path, pool);
		pThis->m_props[std::string(node_name_native)] = apr_hash_copy(pThis->m_pool, prop_hash);
		return error;
	}
	return SVN_NO_ERROR;
}
