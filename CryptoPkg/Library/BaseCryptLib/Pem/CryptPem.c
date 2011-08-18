/** @file
  PEM (Privacy Enhanced Mail) Format Handler Wrapper Implementation over OpenSSL.

Copyright (c) 2010 - 2011, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "InternalCryptLib.h"
#include <openssl/pem.h>

/**
  Callback function for password phrase conversion used for retrieving the encrypted PEM.

  @param[out]  Buf      Pointer to the buffer to write the passphrase to.
  @param[in]   Size     Maximum length of the passphrase (i.e. the size of Buf).
  @param[in]   Flag     A flag which is set to 0 when reading and 1 when writing.
  @param[in]   Key      Key data to be passed to the callback routine.

  @retval  The number of characters in the passphrase or 0 if an error occurred.

**/
INTN
PasswordCallback (
  OUT  CHAR8  *Buf, 
  IN   INTN   Size, 
  IN   INTN   Flag, 
  IN   VOID   *Key
  )
{
  INTN  KeyLength;

  ZeroMem ((VOID *)Buf, (UINTN)Size);
  if (Key != NULL) {
    //
    // Duplicate key phrase directly.
    //
    KeyLength = (INTN) AsciiStrLen ((CHAR8 *)Key);
    KeyLength = (KeyLength > Size ) ? Size : KeyLength;
    CopyMem (Buf, Key, (UINTN) KeyLength);
    return KeyLength;
  } else {
    return 0;
  }
}

/**
  Retrieve the RSA Private Key from the password-protected PEM key data.

  @param[in]  PemData      Pointer to the PEM-encoded key data to be retrieved.
  @param[in]  PemSize      Size of the PEM key data in bytes.
  @param[in]  Password     NULL-terminated passphrase used for encrypted PEM key data.
  @param[out] RsaContext   Pointer to new-generated RSA context which contain the retrieved
                           RSA private key component. Use RsaFree() function to free the
                           resource.

  If PemData is NULL, then ASSERT().
  If RsaContext is NULL, then ASSERT().

  @retval  TRUE   RSA Private Key was retrieved successfully.
  @retval  FALSE  Invalid PEM key data or incorrect password.

**/
BOOLEAN
EFIAPI
RsaGetPrivateKeyFromPem (
  IN   CONST UINT8  *PemData,
  IN   UINTN        PemSize,
  IN   CONST CHAR8  *Password,
  OUT  VOID         **RsaContext
  )
{
  BOOLEAN  Status;
  BIO      *PemBio;

  //
  // ASSERT if PemData is NULL or RsaContext is NULL.
  //
  ASSERT (PemData    != NULL);
  ASSERT (RsaContext != NULL);

  Status = FALSE;
  PemBio = NULL;

  //
  // Add possible block-cipher descriptor for PEM data decryption.
  // NOTE: Only support most popular ciphers (3DES, AES) for the encrypted PEM.
  //
  EVP_add_cipher (EVP_des_ede3_cbc());
  EVP_add_cipher (EVP_aes_128_cbc());
  EVP_add_cipher (EVP_aes_192_cbc());
  EVP_add_cipher (EVP_aes_256_cbc());

  //
  // Read encrypted PEM Data.
  //
  PemBio = BIO_new (BIO_s_mem ());
  BIO_write (PemBio, PemData, (int)PemSize);
  if (PemBio == NULL) {
    goto _Exit;
  }

  //
  // Retrieve RSA Private Key from encrypted PEM data.
  //
  *RsaContext = PEM_read_bio_RSAPrivateKey (PemBio, NULL, (pem_password_cb *)&PasswordCallback, (void *)Password);
  if (*RsaContext != NULL) {
    Status = TRUE;
  }

_Exit:
  //
  // Release Resources.
  //
  BIO_free (PemBio);

  return Status;
}