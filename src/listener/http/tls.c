/*
** Copyright (C) 2014-2016, Eneo Tecnologia S.L.
** Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
** Copyright (C) 2018-2019, Wizzie S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This file is part of n2kafka.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as
** published by the Free Software Foundation, either version 3 of the
** License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "tls.h"

#include <librd/rd.h>
#include <librd/rdlog.h>

#include <assert.h>
#include <stddef.h>
#include <syslog.h>

bool tls_valid_client_certificate(gnutls_session_t tls_session,
				  const char **client_cert_errstr,
				  const char *client_addr) {
	assert(tls_session);
	assert(client_cert_errstr);
	assert(client_addr);

	static const struct {
		const char *errstr;
		gnutls_certificate_status_t flag;
	} gnutls_client_cert_status_errstrs[] = {
			// clang-format off
		{"Certificate is revoked by its authority",
		 GNUTLS_CERT_REVOKED},
		{"The certificate's issuer (signer) is not known",
		 GNUTLS_CERT_SIGNER_NOT_FOUND},
		{"The certificate's signer was not a CA",
		 GNUTLS_CERT_SIGNER_NOT_CA},
		{"The certificate was signed using an insecure algorithm",
		 GNUTLS_CERT_INSECURE_ALGORITHM},
		{"The certificate is not yet activated",
		 GNUTLS_CERT_NOT_ACTIVATED},
		{"The certificate has expired", GNUTLS_CERT_EXPIRED},
		{"The signature verification failed",
		 GNUTLS_CERT_SIGNATURE_FAILURE},
		{"The revocation data are old and have been superseded",
		 GNUTLS_CERT_REVOCATION_DATA_SUPERSEDED},
		{"The owner is not the expected one",
		 GNUTLS_CERT_UNEXPECTED_OWNER},
		{"The revocation data have a future issue date",
		 GNUTLS_CERT_REVOCATION_DATA_ISSUED_IN_FUTURE},
		{"The certificate's signer constraints were violated",
		 GNUTLS_CERT_SIGNER_CONSTRAINTS_FAILURE},
		{"The certificate presented isn't the expected one (TOFU)",
		 GNUTLS_CERT_MISMATCH},
		{"The certificate or an intermediate does not match the "
		 "intended purpose (extended key usage)",
		 GNUTLS_CERT_PURPOSE_MISMATCH},
		{"The certificate requires the server to send the certificate "
		 "status, but no status was received",
		 GNUTLS_CERT_MISSING_OCSP_STATUS},
		{"The received OCSP status response is invalid",
		 GNUTLS_CERT_INVALID_OCSP_STATUS},
		{"The certificate has extensions marked as critical which are "
		 "not supported",
		 GNUTLS_CERT_UNKNOWN_CRIT_EXTENSIONS},
			// clang-format on
	};

	gnutls_certificate_status_t client_cert_status;

	const int verify_rc = gnutls_certificate_verify_peers2(
			tls_session, &client_cert_status);

	if (unlikely(verify_rc != GNUTLS_E_SUCCESS)) {
		*client_cert_errstr = "Unknown error checking certificate, do "
				      "you have one?";
		rdlog(LOG_ERR,
		      "Can't verify client %s certificate, unknown error",
		      client_addr);
		return false;
	}

	if (likely(0 == client_cert_status)) {
		return true;
	}

	for (size_t i = 0; i < RD_ARRAYSIZE(gnutls_client_cert_status_errstrs);
	     ++i) {
		if (gnutls_client_cert_status_errstrs[i].flag &
		    client_cert_status) {
			const char *client_errstr =
					gnutls_client_cert_status_errstrs[i]
							.errstr;

			rdlog(LOG_ERR,
			      "Client %s TLS certificate error: %s",
			      client_addr,
			      client_errstr);
			*client_cert_errstr = client_errstr;

			return false;
		}
	}

	rdlog(LOG_ERR,
	      "Client %s certificate unknown TLS error %x",
	      client_addr,
	      client_cert_status);
	*client_cert_errstr = "Unknown TLS error";
	return false;
}
