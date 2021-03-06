/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

/*
 *  Elliptic curve Diffie-Hellman
 *
 *  Copyright (C) 2006-2015, ARM Limited, All Rights Reserved
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  This file is part of mbed TLS (https://tls.mbed.org)
 */

/*
 * References:
 *
 * SEC1 http://www.secg.org/index.php?action=secg,docs_secg
 * RFC 4492
 */

#include "tls/config.h"

#if defined(MBEDTLS_ECDH_C)

#include "tls/ecdh.h"

#include <string.h>

#if defined(CONFIG_HW_ECDH_PARAM)
#include "tls/see_api.h"
#include "tls/see_internal.h"
#endif

/*
 * Generate public key: simple wrapper around mbedtls_ecp_gen_keypair
 */
int mbedtls_ecdh_gen_public(mbedtls_ecp_group *grp, mbedtls_mpi *d, mbedtls_ecp_point *Q, int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
#if defined(CONFIG_HW_ECDH_PARAM)
	return hw_ecp_gen_keypair(grp, d, Q);
#else
	return mbedtls_ecp_gen_keypair(grp, d, Q, f_rng, p_rng);
#endif
}

/*
 * Compute shared secret (SEC1 3.3.1)
 */
int mbedtls_ecdh_compute_shared(mbedtls_ecp_group *grp, mbedtls_mpi *z, const mbedtls_ecp_point *Q, const mbedtls_mpi *d, int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
	int ret;

#if defined(CONFIG_HW_ECDH_PARAM)
	return hw_ecdh_compute_shared(grp, z, Q);
#else
	mbedtls_ecp_point P;

	mbedtls_ecp_point_init(&P);

	/*
	 * Make sure Q is a valid pubkey before using it
	 */
	MBEDTLS_MPI_CHK(mbedtls_ecp_check_pubkey(grp, Q));

	MBEDTLS_MPI_CHK(mbedtls_ecp_mul(grp, &P, d, Q, f_rng, p_rng));

	if (mbedtls_ecp_is_zero(&P)) {
		ret = MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
		goto cleanup;
	}

	MBEDTLS_MPI_CHK(mbedtls_mpi_copy(z, &P.X));

cleanup:
	mbedtls_ecp_point_free(&P);
#endif
	return (ret);
}

/*
 * Initialize context
 */
void mbedtls_ecdh_init(mbedtls_ecdh_context *ctx)
{
	memset(ctx, 0, sizeof(mbedtls_ecdh_context));
}

/*
 * Free context
 */
void mbedtls_ecdh_free(mbedtls_ecdh_context *ctx)
{
	if (ctx == NULL) {
		return;
	}

#if defined(CONFIG_HW_ECDH_PARAM)
	if (ctx->grp.key_buf) {
		memset(ctx->grp.key_buf, 0, SEE_MAX_ENCRYPTED_KEY_SIZE);
		free(ctx->grp.key_buf);
		ctx->grp.key_buf = NULL;
	}
#endif

	mbedtls_ecp_group_free(&ctx->grp);
	mbedtls_ecp_point_free(&ctx->Q);
	mbedtls_ecp_point_free(&ctx->Qp);
	mbedtls_ecp_point_free(&ctx->Vi);
	mbedtls_ecp_point_free(&ctx->Vf);
	mbedtls_mpi_free(&ctx->d);
	mbedtls_mpi_free(&ctx->z);
	mbedtls_mpi_free(&ctx->_d);
}

/*
 * Setup and write the ServerKeyExhange parameters (RFC 4492)
 *      struct {
 *          ECParameters    curve_params;
 *          ECPoint         public;
 *      } ServerECDHParams;
 */
int mbedtls_ecdh_make_params(mbedtls_ecdh_context *ctx, size_t *olen, unsigned char *buf, size_t blen, int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
	int ret;
	size_t grp_len, pt_len;

	if (ctx == NULL || ctx->grp.pbits == 0) {
		return (MBEDTLS_ERR_ECP_BAD_INPUT_DATA);
	}

	if ((ret = mbedtls_ecdh_gen_public(&ctx->grp, &ctx->d, &ctx->Q, f_rng, p_rng))
		!= 0) {
		return (ret);
	}

	if ((ret = mbedtls_ecp_tls_write_group(&ctx->grp, &grp_len, buf, blen))
		!= 0) {
		return (ret);
	}

	buf += grp_len;
	blen -= grp_len;

	if ((ret = mbedtls_ecp_tls_write_point(&ctx->grp, &ctx->Q, ctx->point_format, &pt_len, buf, blen)) != 0) {
		return (ret);
	}

	*olen = grp_len + pt_len;
	return (0);
}

/*
 * Read the ServerKeyExhange parameters (RFC 4492)
 *      struct {
 *          ECParameters    curve_params;
 *          ECPoint         public;
 *      } ServerECDHParams;
 */
int mbedtls_ecdh_read_params(mbedtls_ecdh_context *ctx, const unsigned char **buf, const unsigned char *end)
{
	int ret;

	if ((ret = mbedtls_ecp_tls_read_group(&ctx->grp, buf, end - *buf)) != 0) {
		return (ret);
	}

	if ((ret = mbedtls_ecp_tls_read_point(&ctx->grp, &ctx->Qp, buf, end - *buf))
		!= 0) {
		return (ret);
	}

	return (0);
}

/*
 * Get parameters from a keypair
 */
int mbedtls_ecdh_get_params(mbedtls_ecdh_context *ctx, const mbedtls_ecp_keypair *key, mbedtls_ecdh_side side)
{
	int ret;

	if ((ret = mbedtls_ecp_group_copy(&ctx->grp, &key->grp)) != 0) {
		return (ret);
	}

	/* If it's not our key, just import the public part as Qp */
	if (side == MBEDTLS_ECDH_THEIRS) {
		return (mbedtls_ecp_copy(&ctx->Qp, &key->Q));
	}

	/* Our key: import public (as Q) and private parts */
	if (side != MBEDTLS_ECDH_OURS) {
		return (MBEDTLS_ERR_ECP_BAD_INPUT_DATA);
	}

	if ((ret = mbedtls_ecp_copy(&ctx->Q, &key->Q)) != 0 || (ret = mbedtls_mpi_copy(&ctx->d, &key->d)) != 0) {
		return (ret);
	}

#if defined(CONFIG_HW_ECDSA_SIGN)
	ctx->grp.key_index = key->key_index;
#endif

	return (0);
}

/*
 * Setup and export the client public value
 */
int mbedtls_ecdh_make_public(mbedtls_ecdh_context *ctx, size_t *olen, unsigned char *buf, size_t blen, int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
	int ret;

	if (ctx == NULL || ctx->grp.pbits == 0) {
		return (MBEDTLS_ERR_ECP_BAD_INPUT_DATA);
	}

	if ((ret = mbedtls_ecdh_gen_public(&ctx->grp, &ctx->d, &ctx->Q, f_rng, p_rng))
		!= 0) {
		return (ret);
	}

	return mbedtls_ecp_tls_write_point(&ctx->grp, &ctx->Q, ctx->point_format, olen, buf, blen);
}

/*
 * Parse and import the client's public value
 */
int mbedtls_ecdh_read_public(mbedtls_ecdh_context *ctx, const unsigned char *buf, size_t blen)
{
	int ret;
	const unsigned char *p = buf;

	if (ctx == NULL) {
		return (MBEDTLS_ERR_ECP_BAD_INPUT_DATA);
	}

	if ((ret = mbedtls_ecp_tls_read_point(&ctx->grp, &ctx->Qp, &p, blen)) != 0) {
		return (ret);
	}

	if ((size_t)(p - buf) != blen) {
		return (MBEDTLS_ERR_ECP_BAD_INPUT_DATA);
	}

	return (0);
}

/*
 * Derive and export the shared secret
 */
int mbedtls_ecdh_calc_secret(mbedtls_ecdh_context *ctx, size_t *olen, unsigned char *buf, size_t blen, int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
	int ret;

	if (ctx == NULL) {
		return (MBEDTLS_ERR_ECP_BAD_INPUT_DATA);
	}

	if ((ret = mbedtls_ecdh_compute_shared(&ctx->grp, &ctx->z, &ctx->Qp, &ctx->d, f_rng, p_rng)) != 0) {
		return (ret);
	}

	if (mbedtls_mpi_size(&ctx->z) > blen) {
		return (MBEDTLS_ERR_ECP_BAD_INPUT_DATA);
	}

	*olen = ctx->grp.pbits / 8 + ((ctx->grp.pbits % 8) != 0);
	return mbedtls_mpi_write_binary(&ctx->z, buf, *olen);
}

#if defined(CONFIG_HW_ECDH_PARAM)
int hw_ecp_gen_keypair(mbedtls_ecp_group *grp, mbedtls_mpi *d, mbedtls_ecp_point *Q)
{
	unsigned int ret;
	unsigned int key_type = ECC_KEY;
	unsigned int curve;

	switch (grp->id) {
	case MBEDTLS_ECP_DP_SECP192R1:
		curve = OID_ECC_P192;
		break;
	case MBEDTLS_ECP_DP_SECP224R1:
		curve = OID_ECC_P224;
		break;
	case MBEDTLS_ECP_DP_SECP256R1:
		curve = OID_ECC_P256;
		break;
	case MBEDTLS_ECP_DP_SECP384R1:
		curve = OID_ECC_P384;
		break;
	case MBEDTLS_ECP_DP_SECP521R1:
		curve = OID_ECC_P521;
		break;
	case MBEDTLS_ECP_DP_BP256R1:
		curve = OID_ECC_BP256;
		break;
	default:
		ret = MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE;
		goto cleanup;
	}

	if (grp->key_buf == NULL) {
		grp->key_buf = (unsigned char *)malloc(SEE_MAX_ENCRYPTED_KEY_SIZE);
		if (grp->key_buf == NULL) {
			return MBEDTLS_ERR_ECP_ALLOC_FAILED;
		}
	}

	if ((ret = see_generate_key_internal(key_type | curve, grp->key_buf, 0, 0)) != 0) {
		return MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE;
	}

	struct sECC_KEY ecc_pub;

	memset(&ecc_pub, 0, sizeof(struct sECC_KEY));

	ecc_pub.publickey_x = (unsigned char *)malloc(SEE_MAX_ECP_KEY_SIZE);
	ecc_pub.publickey_y = (unsigned char *)malloc(SEE_MAX_ECP_KEY_SIZE);

	if (ecc_pub.publickey_x == NULL) {
		return MBEDTLS_ERR_ECP_ALLOC_FAILED;
	}

	if (ecc_pub.publickey_y == NULL) {
		free(ecc_pub.publickey_x);
		return MBEDTLS_ERR_ECP_ALLOC_FAILED;
	}

	/* Get Public value from sss */
	if ((ret = see_get_ecc_publickey_internal(&ecc_pub, grp->key_buf, curve)) != 0) {
		ret = MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE;
		goto cleanup;
	}

	/* Copy pub value to Q */
	MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&Q->X, ecc_pub.publickey_x, ecc_pub.x_byte_len));
	MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(&Q->Y, ecc_pub.publickey_y, ecc_pub.y_byte_len));
	MBEDTLS_MPI_CHK(mbedtls_mpi_lset(&Q->Z, 1));

cleanup:
	if (ecc_pub.publickey_x) {
		free(ecc_pub.publickey_x);
	}

	if (ecc_pub.publickey_y) {
		free(ecc_pub.publickey_y);
	}

	return (ret);
}
int hw_ecdh_compute_shared(mbedtls_ecp_group *grp, mbedtls_mpi *z, const mbedtls_ecp_point *Q)
{
	int ret;
	struct sECC_KEY ecc_pub;
	unsigned int olen = SEE_MAX_ECP_KEY_SIZE;
	unsigned char output[SEE_MAX_ECP_KEY_SIZE];

	memset(&ecc_pub, 0, sizeof(struct sECC_KEY));

	ecc_pub.x_byte_len = mbedtls_mpi_size(&Q->X);
	ecc_pub.y_byte_len = mbedtls_mpi_size(&Q->Y);

	if (!(ecc_pub.publickey_x = (unsigned char *)malloc(ecc_pub.x_byte_len))) {
		return MBEDTLS_ERR_ECP_ALLOC_FAILED;
	}

	if (!(ecc_pub.publickey_y = (unsigned char *)malloc(ecc_pub.y_byte_len))) {
		free(ecc_pub.publickey_x);
		return MBEDTLS_ERR_ECP_ALLOC_FAILED;
	}

	MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&Q->X, ecc_pub.publickey_x, ecc_pub.x_byte_len));
	MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&Q->Y, ecc_pub.publickey_y, ecc_pub.y_byte_len));

	switch (grp->id) {
	case MBEDTLS_ECP_DP_SECP192R1:
		ecc_pub.curve |= OID_ECC_P192;
		break;
	case MBEDTLS_ECP_DP_SECP224R1:
		ecc_pub.curve |= OID_ECC_P224;
		break;
	case MBEDTLS_ECP_DP_SECP256R1:
		ecc_pub.curve |= OID_ECC_P256;
		break;
	case MBEDTLS_ECP_DP_SECP384R1:
		ecc_pub.curve |= OID_ECC_P384;
		break;
	case MBEDTLS_ECP_DP_SECP521R1:
		ecc_pub.curve |= OID_ECC_P521;
		break;
	case MBEDTLS_ECP_DP_BP256R1:
		ecc_pub.curve |= OID_ECC_BP256;
		break;
	default:
		ret = MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
		goto cleanup;
	}

#if defined(CONFIG_HW_ECDSA_SIGN)
	/* Compute P  = d * Q */
	if (grp->key_buf == NULL) {
		/* compute ECC shared secret with stored key (permanent) */
		if ((ret = see_compute_ecdh_param(&ecc_pub, grp->key_index, output, &olen)) != 0) {
			goto cleanup;
		}
	} else
#endif
	{
		/* compute ECC shared secret with generated key (temporary) */
		if ((ret = see_compute_ecdh_param_internal(&ecc_pub, grp->key_buf, output, &olen)) != 0) {
			goto cleanup;
		}
	}

	MBEDTLS_MPI_CHK(mbedtls_mpi_read_binary(z, output, olen));

cleanup:
	if (ecc_pub.publickey_x) {
		free(ecc_pub.publickey_x);
	}

	if (ecc_pub.publickey_y) {
		free(ecc_pub.publickey_y);
	}

	return ret;
}
#endif /* CONFIG_HW_ECDH_PARAM */
#endif /* MBEDTLS_ECDH_C */
