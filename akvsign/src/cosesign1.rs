// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

use minicbor::{CborLen, Decode, Encode, Encoder};

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum CborError {
    /// CBOR decode error
    #[allow(dead_code)]
    DecodeError,
    #[warn(dead_code)]

    /// CBOR encode error
    EncodeError,
}

/// 0b110 (major type 6 for tag) | 10010 (CBOR Tag 18 for COSE_Sign1) \ h'd2
pub const COSE_SIGN1_TAG: u8 = 0xd2;

/// The size of the tag.
pub const COSE_SIGN1_TAG_SIZE: usize = 1;

/// The size of the encoded protected header.
pub const PROTECTED_HEADER_SIZE: usize = 23;

// CBOR-encoded protected header with alg RSA256 and content type as "application/json".
pub const PROTECTED_HEADER: [u8; PROTECTED_HEADER_SIZE] = [
    0xa2, // Start of map with 2 key-value pairs
    0x01, // Key: alg (1)
    0x39, 0x01, 0x00, // Value: -257 (RS256)
    0x03, // Key: content_type (3)
    0x70, // Text string (major type 3, length 16)
    0x61, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x2f, 0x6a, 0x73, 0x6f,
    0x6e, // "application/json"
];

/// Signature size for RSA 2k.
pub const SIGNATURE_SIZE: usize = 256;

/// Maximum size of a public key in DER format.
pub const PUB_KEY_MAX_SIZE: usize = 312;

/// Definition of COSE_Sign1 object.
/// See <https://www.rfc-editor.org/rfc/rfc9052> for more details.
#[derive(Encode, CborLen, Decode)]
#[cbor(array)]
pub struct CoseSign1Object<'a> {
    /// CBOR-encoded ProtectedHeader and wrapped in
    /// a bstr object.
    #[n(0)]
    #[cbor(with = "minicbor::bytes")]
    pub protected_header: [u8; PROTECTED_HEADER_SIZE],

    /// CBOR map.
    #[n(1)]
    pub unprotected_header: UnprotectedHeader<'a>,

    /// CBOR-encoded payload and wrapped
    /// in a bstr object.
    #[n(2)]
    #[cbor(with = "minicbor::bytes")]
    pub payload: &'a [u8],

    /// bstr.
    #[n(3)]
    #[cbor(with = "minicbor::bytes")]
    pub signature: [u8; SIGNATURE_SIZE],
}

impl<'a> CoseSign1Object<'a> {
    /// CBOR encoding of the struct.
    pub fn encode(&self, out: &mut Vec<u8>) -> Result<usize, CborError> {
        // Add the COSE_Sign1 tag.
        out[0] = COSE_SIGN1_TAG;

        minicbor::encode(self, &mut out[COSE_SIGN1_TAG_SIZE..])
            .map_err(|_| CborError::EncodeError)?;

        Ok(COSE_SIGN1_TAG_SIZE + minicbor::len(self))
    }
}

/// Definition of unprotected header.
#[derive(Copy, Clone, Encode, CborLen, Decode)]
#[cbor(map)]
pub struct UnprotectedHeader<'a> {
    #[cbor(n(248))]
    #[cbor(with = "minicbor::bytes")]
    pub signer_pub_key_der: &'a [u8],
}

impl<'a> UnprotectedHeader<'a> {
    pub fn new() -> Self {
        UnprotectedHeader {
            signer_pub_key_der: &[],
        }
    }

    pub fn new_with_der(pub_key: &'a Vec<u8>) -> Self {
        let mut signer_pub_key_der = [0u8; PUB_KEY_MAX_SIZE];
        signer_pub_key_der[..pub_key.len()].copy_from_slice(pub_key);
        UnprotectedHeader {
            signer_pub_key_der: pub_key.as_slice(),
        }
    }
}

/// CBOR-encoded "Signature1".
pub const SIG_STRUCTURE_CONTEXT: [u8; SIG_STRUCTURE_CONTEXT_SIZE] =
    [0x53, 0x69, 0x67, 0x6e, 0x61, 0x74, 0x75, 0x72, 0x65, 0x31];

/// Size of CBOR-encoded "Signature1".
pub const SIG_STRUCTURE_CONTEXT_SIZE: usize = 10;

/// Extra bytes reserved for `SigSignature` encoding.
/// 1 byte: type array (0b100) | size 4 (00100) \ h'84
/// 1 byte:   type str (0b011) | size 10 (01010) \ h'6a
/// 1 byte:   type bstr (0b010) | size 22 \ h'56
/// 1 byte:   type bstr (0b010) | size 0 \ h'40
/// 3 bytes:  type bstr (0b010) | size 689 \ h'5902b1
pub const SIG_STRUCTURE_ENCODING_BYTES: usize = 7;

/// The encoding byte of a 4-entries array.
pub const COSE_SIGN1_ARRAY_4: u8 = 0x84;

/// The encoding byte of 10-length string.
pub const COSE_SIGN1_STR_10: u8 = 0x6a;

/// Create the encoding of `SigStructure`.
/// See Section 4.4, <https://www.rfc-editor.org/rfc/rfc9052> for more details.
/// / SigStructure / = [
///     / context / "Signature1",
///     / body_protected / protected_header in bstr,
///     / external_aad / empty bstr,
///     / payload / payload in bstr,
/// ]
pub fn encode_sig_struct(
    body_protected: &[u8],
    payload: &[u8],
    out: &mut [u8],
) -> Result<usize, CborError> {
    let out_size = out.len();
    let mut index = 0;

    // Manual encoding of leading bytes to remove the str encoding
    // dependency from minicbor.
    out[index] = COSE_SIGN1_ARRAY_4;
    index += 1;
    out[index] = COSE_SIGN1_STR_10;
    index += 1;
    out[index..index + SIG_STRUCTURE_CONTEXT_SIZE].copy_from_slice(&SIG_STRUCTURE_CONTEXT[..]);
    index += SIG_STRUCTURE_CONTEXT_SIZE;

    let buffer_size = out_size - index;
    let mut encoder = Encoder::new(&mut out[index..]);

    encoder
        .bytes(body_protected)
        .map_err(|_| CborError::EncodeError)?
        .bytes(&[])
        .map_err(|_| CborError::EncodeError)?
        .bytes(payload)
        .map_err(|_| CborError::EncodeError)?;
    let payload_size = buffer_size - encoder.writer().len();

    Ok(index + payload_size)
}

/// Support COSE_Sign1 object creation based on <https://www.rfc-editor.org/rfc/rfc9052>.
pub(crate) struct CoseSign1 {
    protected_header: [u8; PROTECTED_HEADER_SIZE],
    payload: Vec<u8>,
    signer_pub_key_der: Option<Vec<u8>>,
}

impl CoseSign1 {
    ///  Create and initialize a `Cose_Sign1` instance.
    ///
    /// # Returns
    /// * `Cose_Sign1` - An initialized `Cose_Sign1` instance.
    pub(crate) fn new(payload: Vec<u8>, signer_pub_key_der: Option<Vec<u8>>) -> Self {
        Self {
            protected_header: PROTECTED_HEADER,
            payload,
            signer_pub_key_der,
        }
    }

    fn sig_struct_size(&self) -> usize {
        SIG_STRUCTURE_ENCODING_BYTES
            + SIG_STRUCTURE_CONTEXT_SIZE
            + PROTECTED_HEADER_SIZE
            + self.payload.len()
    }

    /// Create the to-be-signed buffer based on Section 4.4, <https://www.rfc-editor.org/rfc/rfc9052>.
    /// # Returns
    /// * `Vec<u8>` - The Sig_structure buffer.
    ///
    /// # Errors
    /// * `CborError` - If CBOR encoding fails during creation.
    pub fn create_tbs(&self) -> Result<Vec<u8>, CborError> {
        let mut sig_struct_buffer = Vec::with_capacity(self.sig_struct_size());
        sig_struct_buffer.resize(self.sig_struct_size(), 0);

        encode_sig_struct(
            &self.protected_header,
            &self.payload,
            &mut sig_struct_buffer,
        )
        .map_err(|_| CborError::EncodeError)?;

        Ok(sig_struct_buffer)
    }

    fn tagged_cose_sign1_object_size(&self, obj: &CoseSign1Object) -> usize {
        COSE_SIGN1_TAG_SIZE + minicbor::len(obj)
    }

    pub fn create_cose_sign1_object(
        &self,
        signature: &[u8; SIGNATURE_SIZE],
    ) -> Result<Vec<u8>, CborError> {
        let unprotected_header = match &self.signer_pub_key_der {
            Some(pub_key) => UnprotectedHeader::new_with_der(&pub_key),
            None => UnprotectedHeader::new(),
        };
        let cose_sign1 = CoseSign1Object {
            protected_header: self.protected_header,
            unprotected_header: unprotected_header,
            payload: &self.payload.clone(),
            signature: *signature,
        };

        let mut cose_sign1_buffer =
            Vec::with_capacity(self.tagged_cose_sign1_object_size(&cose_sign1));
        cose_sign1_buffer.resize(self.tagged_cose_sign1_object_size(&cose_sign1), 0);

        cose_sign1
            .encode(&mut cose_sign1_buffer)
            .map_err(|_| CborError::EncodeError)?;

        Ok(cose_sign1_buffer)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use minicbor::Decoder;
    use openssl::hash::{Hasher, MessageDigest};
    use openssl::pkey::PKey;
    use openssl::rsa::Rsa;
    use openssl::sign::{Signer, Verifier};

    #[test]
    fn test_create_cose_sign1_object() {
        // Generate RSA key pair
        let rsa = Rsa::generate(2048).expect("failed to generate RSA key");
        let pkey = PKey::from_rsa(rsa).expect("failed to create PKey");
        let public_key_der = pkey
            .public_key_to_der()
            .expect("failed to encode public key");

        // Create CoseSign1 object
        let payload = b"test payload".to_vec();
        let cose_sign1 = CoseSign1::new(payload.clone(), Some(public_key_der));
        let tbs = cose_sign1
            .create_tbs()
            .expect("Failed to create to-be-signed data");

        let mut hasher = Hasher::new(MessageDigest::sha256()).unwrap();
        hasher.update(&tbs).unwrap();
        let hashed_data = hasher.finish().unwrap();

        // Sign the TBS buffer
        let mut signer =
            Signer::new(MessageDigest::sha256(), &pkey).expect("failed to create signer");
        signer
            .update(&hashed_data)
            .expect("failed to update signer");
        let signature = signer.sign_to_vec().expect("failed to sign");

        // Create cbor encoded cose_sign1 object
        let fixed_signature: &[u8; 256] = signature
            .as_slice()
            .try_into()
            .expect("Signature must be exactly 256 bytes long");
        let cose_sign1_buffer = cose_sign1
            .create_cose_sign1_object(fixed_signature)
            .unwrap();

        // Check that the buffer starts with the COSE_Sign1 tag
        assert_eq!(cose_sign1_buffer[0], COSE_SIGN1_TAG);

        // encode cose_sign1_buffer as CoseSign1Object
        let mut decoder = Decoder::new(&cose_sign1_buffer[COSE_SIGN1_TAG_SIZE..]);
        let cose_sign1_obj: CoseSign1Object = decoder.decode().unwrap();
        assert_eq!(cose_sign1_obj.payload, payload);

        // create tbs from CoseSign1Object and hash it
        let c2 = CoseSign1::new(
            cose_sign1_obj.payload.to_vec(),
            Some(
                cose_sign1_obj
                    .unprotected_header
                    .signer_pub_key_der
                    .to_vec(),
            ),
        );
        let tbs2 = c2.create_tbs().expect("Failed to create to-be-signed data");
        assert_eq!(tbs, tbs2);
        let mut hasher2 = Hasher::new(MessageDigest::sha256()).unwrap();
        hasher2.update(&tbs2).unwrap();
        let hashed_data2 = hasher2.finish().unwrap();

        // Verify signature with pub key in unprotected header
        let pub_key_from_unprotected_header =
            PKey::public_key_from_der(&cose_sign1_obj.unprotected_header.signer_pub_key_der)
                .unwrap();
        let mut verifier =
            Verifier::new(MessageDigest::sha256(), &pub_key_from_unprotected_header).unwrap();
        verifier.update(&hashed_data2).unwrap();
        let is_valid = verifier.verify(&cose_sign1_obj.signature).unwrap();
        assert!(is_valid);
    }
}
