// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

use azure_security_keyvault::prelude::*;
use azure_security_keyvault::KeyClient;
use base64::{engine::general_purpose, Engine as _};
use openssl::hash::{Hasher, MessageDigest};
use openssl::pkey::{PKey, Public};
use openssl::rsa::Rsa;
use openssl::sha::sha256;
use openssl::sign::Verifier;
use std::env;
use std::error::Error;
use std::fs::{self, File};
use std::io::Read;
use structopt::StructOpt;

mod cosesign1;

const USAGE: &str = r#"
AKV Signing Tool

USAGE:
    akvsign <INPUT_FILE> --output-format <FORMAT> [--verify]

ARGS:
    <INPUT_FILE>    Path to the file to be signed

OPTIONS:
    --output-format <FORMAT>    Output format for the signature [possible values: raw, cosesign1]
    --verify                    Verify the signature after signing

ENVIRONMENT VARIABLES:
    KEYVAULT_URL    URL of the Azure Key Vault (required)
    KEY_NAME        Name of the key in Azure Key Vault to use for signing (required)

DESCRIPTION:
    This tool signs a given file using a key stored in Azure Key Vault. It can output the signature
    in either raw format or as a COSE_Sign1 structure. Optionally, it can verify the signature
    after signing.

OUTPUTS:
    - <INPUT_FILE>.pub: Contains the public key in PEM format.
    - <INPUT_FILE>.sig: Contains the signature (raw or COSE_Sign1 format).

EXAMPLES:
    Export KEYVAULT_URL and KEY_NAME environment variables:
        export KEYVAULT_URL=https://your-keyvault.vault.azure.net
        export KEY_NAME=your-key-name

    Sign a file and output in raw format:
        akvsign path/to/your/file --output-format raw

    Sign a file, output in COSE_Sign1 format, and verify the signature:
        akvsign path/to/your/file --output-format cosesign1 --verify

NOTE:
    This tool requires Azure credentials to be set up in your environment. It uses the
    DefaultAzureCredential from the azure_identity crate for authentication.
"#;

#[derive(StructOpt)]
#[structopt(name = "akvsign", about = "A tool to sign files using Azure Key Vault")]
struct Opt {
    /// Input file to sign
    #[structopt(parse(from_os_str))]
    input: Option<std::path::PathBuf>,

    /// Output format
    #[structopt(long, possible_values = &["raw", "cosesign1"])]
    output_format: Option<String>,

    /// Verify signature
    #[structopt(long)]
    verify: bool,

    /// Print usage information
    #[structopt(long, help = "Print detailed usage information")]
    help: bool,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let opt = Opt::from_args();

    if opt.help {
        println!("{}", USAGE);
        return Ok(());
    }

    let input_path = opt.input.expect("Input file path not provided!");
    let is_cose_sign1_out_fmt = match opt.output_format {
        Some(ref s) if s == "cosesign1" => true,
        _ => false,
    };

    let vault_url = env::var("KEYVAULT_URL").expect("Missing KEYVAULT_URL environment variable");
    let key_name = env::var("KEY_NAME").expect("Missing KEY_NAME environment variable");
    let credential = azure_identity::create_credential()?;
    let key_client = KeyClient::new(&vault_url, credential)?;

    // Fetch public key
    let public_key = get_public_key_from_akv(&key_client, &key_name).await?;
    // Calculate signer public key hash and write to file
    let signer_pub_key_hash = signer_public_key_hash(&public_key)?;
    let mut public_key_path = input_path.clone();
    public_key_path.set_extension("signerpubkeyhash");
    fs::write(&public_key_path, &signer_pub_key_hash)?;
    println!(
        "Signer's public key hash written to: {}",
        public_key_path.display()
    );

    // Read input file
    let mut file = File::open(&input_path)?;
    let mut data = Vec::new();
    file.read_to_end(&mut data)?;
    let mut cose_sign1_doc: Option<cosesign1::CoseSign1> = None;

    // Calculate sha256 of input file contents and convert to base64 url encode
    if is_cose_sign1_out_fmt {
        let c = cosesign1::CoseSign1::new(data, Some(public_key.public_key_to_der()?));
        let tbs = c.create_tbs().expect("Failed to create to-be-signed data");
        data = tbs.to_vec();
        cose_sign1_doc = Some(c);
    }

    let mut hasher = Hasher::new(MessageDigest::sha256())?;
    hasher.update(&data)?;
    let hashed_data = hasher.finish()?;
    let data_to_sign = general_purpose::URL_SAFE_NO_PAD.encode(hashed_data);

    // Send sign request to AKV with base64url encoded digest
    let sign_result = key_client
        .sign(&key_name, SignatureAlgorithm::RS256, &data_to_sign)
        .await?;
    let signature = sign_result.signature;

    // Write signature output
    if is_cose_sign1_out_fmt {
        // Construct Cose_Sign1 object
        let fixed_signature: &[u8; 256] = signature
            .as_slice()
            .try_into()
            .expect("Signature must be exactly 256 bytes long");
        let cose_sign1 = cose_sign1_doc.expect("COSE_Sign1 document not found!");
        let cose_sign1_obj = cose_sign1
            .create_cose_sign1_object(fixed_signature)
            .expect("Failed to create COSE_Sign1 object");

        // Write COSE_Sign1 object to file
        let mut output_path = input_path.clone();
        output_path.set_extension("cosesign1");
        fs::write(&output_path, &cose_sign1_obj)?;
        println!("COSE_Sign1 object written to: {}", output_path.display());
    } else {
        // Write signature to file
        let mut output_path = input_path.clone();
        output_path.set_extension("sig");
        fs::write(&output_path, &signature)?;
        println!("Signature written to: {}", output_path.display());

        // Write public key to file
        let mut public_key_path = input_path.clone();
        public_key_path.set_extension("pub");
        fs::write(&public_key_path, &public_key.public_key_to_pem()?)?;
        println!("Public key written to: {}", public_key_path.display());
    }

    // Optional step: Verify signature
    if !is_cose_sign1_out_fmt && opt.verify {
        match verify_signature(&data, &signature, &public_key) {
            Ok(true) => println!("Signature verification successful!"),
            Ok(false) => println!("Signature verification failed!"),
            Err(e) => println!("Error during signature verification: {}", e),
        }
    }

    Ok(())
}

async fn get_public_key_from_akv(
    key_client: &KeyClient,
    key_name: &str,
) -> Result<Rsa<openssl::pkey::Public>, Box<dyn Error>> {
    let key = key_client.get(key_name).await?;
    let n = key.key.n.ok_or("Failed to get public key modulus")?;
    let e = key.key.e.ok_or("Failed to get public key exponent")?;

    let rsa = Rsa::from_public_components(
        openssl::bn::BigNum::from_slice(&n)?,
        openssl::bn::BigNum::from_slice(&e)?,
    )?;

    Ok(rsa)
}

fn verify_signature(
    data: &[u8],
    signature: &[u8],
    public_key: &Rsa<openssl::pkey::Public>,
) -> Result<bool, Box<dyn Error>> {
    let pkey = PKey::from_rsa(public_key.clone())?;
    let mut verifier = Verifier::new(MessageDigest::sha256(), &pkey)?;
    verifier.update(data)?;
    Ok(verifier.verify(signature)?)
}

fn signer_public_key_hash(public_key: &Rsa<Public>) -> Result<Vec<u8>, Box<dyn Error>> {
    let n = public_key.n().to_vec();
    let e = public_key.e().to_vec();

    let mut concatenated = n;
    concatenated.extend(e);
    let hash = sha256(&concatenated);
    let hexstring: String = hash.iter().map(|byte| format!("{:02x}", byte)).collect();

    println!("Signer public key hash: {}", hexstring);
    Ok(hash.to_vec())
}
