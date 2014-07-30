#get balance
import sys
import json
import time
import random
import hashlib
import operator
import bitcoinrpc
import pybitcointools
from decimal import *

#this is not a command line tool. do not use it as such

if len(sys.argv) < 3: 
    exit()

force=False

listOptions = { 
        'transaction_type': int(sys.argv[1]), 
        'ecosystem': int(sys.argv[2]),
        "property_type": int(sys.argv[3]),
        "previous_property_id": int(sys.argv[4]),
        "property_category": sys.argv[5],
        "property_subcategory": sys.argv[6],
        "property_name": sys.argv[7],
        "property_url": sys.argv[8],
        "property_data": sys.argv[9],
        "currency_identifier_desired": int(sys.argv[10]),
        "number_properties": int(sys.argv[11]),
        "deadline": int(sys.argv[12]),
        "earlybird_bonus": int(sys.argv[13]),
        "percentage_for_issuer": int(sys.argv[14]),
        "transaction_from": sys.argv[15],
        "from_private_key": sys.argv[16]
    }


HEXSPACE_SECOND='21'
testnet=True
if testnet:
    magicbyte=111
#import pprint
#pprint.pprint(listOptions)

conndetails=open(sys.argv[17]).readline().split(':')

#sort out whether using local or remote API
conn = bitcoinrpc.connect_to_remote(conndetails[0],conndetails[1],host=conndetails[2],port=conndetails[3],use_https=int(conndetails[4]))

#print '\n conndet', conndetails, conn.getinfo()
#check if private key provided produces correct address
address = pybitcointools.privkey_to_address(listOptions['from_private_key'], magicbyte)
if not address == listOptions['transaction_from'] and not force:
    print json.dumps({ "status": "NOT OK", "error": "Private key does not produce same address as \'transaction from\'" , "fix": "Set \'force\' flag to proceed without address checks" })
    exit()

#see if account has been added
account = conn.getaccount(listOptions['transaction_from'])
if account == "" and not force:
    _time = str(int(time.time()))
    private = listOptions['from_private_key']
    print json.dumps({ "status": "NOT OK", "error": "Couldn\'t find address in wallet, please run \'fix\' on the machine", "fix": "bitcoind importprivkey " + private + " imported_" + _time  })

#calculate minimum unspent balance
available_balance = Decimal(0.0)

unspent_tx = []
for unspent in conn.listunspent():
    if unspent.address == listOptions['transaction_from']:
        unspent_tx.append(unspent)
#get all unspent for our from_address

for unspent in unspent_tx:
   available_balance = unspent.amount + available_balance

#check if minimum BTC balance is met
if available_balance < Decimal(0.00006*3) and not force:
    print json.dumps({ "status": "NOT OK", "error": "Not enough funds" , "fix": "Set \'force\' flag to proceed without balance checks" })
    exit()

#generate public key of bitcoin address 
validated = conn.validateaddress(listOptions['transaction_from'])
if 'pubkey' in validated.__dict__: 
    pubkey = validated.pubkey

elif not force:
    print json.dumps({ "status": "NOT OK", "error": "from address is invalid or hasn't been used on the network" , "fix": "Set \'force\' flag to proceed without balance checks" })
    exit()

#find largest spendable input from UTXO
largest_spendable_input = { "txid": "", "amount": Decimal(0) }
for unspent in unspent_tx:
    if unspent.amount > largest_spendable_input["amount"]:
        largest_spendable_input = { "txid": unspent.txid, "amount": unspent.amount }

#real stuff happens here:

#build multisig data address
transaction_version = 0
transaction_type = listOptions['transaction_type']
ecosystem = listOptions['ecosystem']
property_type = listOptions['property_type']
previous_property_id = listOptions['previous_property_id']
property_category = listOptions['property_category']
property_subcategory = listOptions['property_subcategory']
property_name = listOptions['property_name']
property_url = listOptions['property_url']
property_data = listOptions['property_data']
currency_identifier_desired = listOptions['currency_identifier_desired']
number_properties = listOptions['number_properties']
deadline = listOptions['deadline']
earlybird_bonus = listOptions['earlybird_bonus']
percentage_for_issuer = listOptions['percentage_for_issuer']


#calculate bytes
tx_ver_bytes = hex(transaction_version)[2:].rjust(4,"0") # 2 bytes
tx_type_bytes = hex(transaction_type)[2:].rjust(4,"0")   # 2 bytes
eco_bytes = hex(ecosystem)[2:].rjust(2,"0")              # 1 byte
prop_type_bytes = hex(property_type)[2:].rjust(4,"0")    # 2 bytes
prev_prop_id_bytes = hex(previous_property_id)[2:].rjust(8,"0")  # 4 bytes
curr_ident_des_bytes = hex(currency_identifier_desired)[2:].rjust(8,"0")      # 4 bytes
num_prop_bytes = hex(number_properties)[2:].rjust(16,"0")# 8 bytes
deadline_bytes = hex(deadline)[2:].rjust(16,"0")         # 8 bytes
earlybird_bytes = hex(earlybird_bonus)[2:].rjust(2,"0")        # 1 byte
percent_issuer_bytes = hex(percentage_for_issuer)[2:].rjust(2,"0") # 1 byte
prop_cat_bytes = ''                                      # var bytes
prop_subcat_bytes = ''                                   # var bytes
prop_name_bytes = ''                                     # var bytes
prop_url_bytes = ''                                      # var bytes
prop_data_bytes = ''                                     # var bytes

for let in property_category:
    hex_bytes = hex(ord(let))[2:]
    if len(hex_bytes) % 2 == 1:
        hex_bytes = hex_bytes[:len(hex_bytes)-1]
    if len(hex_bytes) > 255:
        hex_bytes = hex_bytes[255:]
    prop_cat_bytes = prop_cat_bytes + hex_bytes
prop_cat_bytes = prop_cat_bytes + '00'

for let in property_subcategory:
    hex_bytes = hex(ord(let))[2:]
    if len(hex_bytes) % 2 == 1:
        hex_bytes = hex_bytes[:len(hex_bytes)-1]
    if len(hex_bytes) > 255:
        hex_bytes = hex_bytes[255:]
    prop_subcat_bytes = prop_subcat_bytes + hex_bytes
prop_subcat_bytes = prop_subcat_bytes + '00'

for let in property_name:
    hex_bytes = hex(ord(let))[2:]
    if len(hex_bytes) % 2 == 1:
        hex_bytes = hex_bytes[:len(hex_bytes)-1]
    if len(hex_bytes) > 255:
        hex_bytes = hex_bytes[255:]
    prop_name_bytes = prop_name_bytes + hex_bytes
prop_name_bytes = prop_name_bytes + '00'

for let in property_url:
    hex_bytes = hex(ord(let))[2:]
    if len(hex_bytes) % 2 == 1:
        hex_bytes = hex_bytes[:len(hex_bytes)-1]
    if len(hex_bytes) > 255:
        hex_bytes = hex_bytes[255:]
    prop_url_bytes = prop_url_bytes + hex_bytes
prop_url_bytes = prop_url_bytes + '00'

for let in property_data:
    hex_bytes = hex(ord(let))[2:]
    if len(hex_bytes) % 2 == 1:
        hex_bytes = hex_bytes[:len(hex_bytes)-1]
    if len(hex_bytes) > 255:
        hex_bytes = hex_bytes[255:]
    prop_data_bytes = prop_data_bytes + hex_bytes
prop_data_bytes = prop_data_bytes + '00'

total_bytes = (len(tx_ver_bytes) + 
               len(tx_type_bytes) + 
               len(eco_bytes) + 
               len(prop_type_bytes) + 
               len(prev_prop_id_bytes) + 
               len(num_prop_bytes) +
               len(curr_ident_des_bytes) +
               len(deadline_bytes) +
               len(earlybird_bytes) +
               len(percent_issuer_bytes) +
               len(prop_cat_bytes) + 
               len(prop_subcat_bytes) + 
               len(prop_name_bytes) + 
               len(prop_url_bytes) + 
               len(prop_data_bytes))/2

byte_stream = (tx_ver_bytes + 
               tx_type_bytes + 
               eco_bytes + 
               prop_type_bytes + 
               prev_prop_id_bytes + 
               prop_cat_bytes + 
               prop_subcat_bytes + 
               prop_name_bytes + 
               prop_url_bytes + 
               prop_data_bytes +
               curr_ident_des_bytes +
               num_prop_bytes +
               deadline_bytes +
               earlybird_bytes +
               percent_issuer_bytes)

#DEBUG print [tx_ver_bytes,tx_type_bytes,eco_bytes,prop_type_bytes,prev_prop_id_bytes,num_prop_bytes,prop_cat_bytes,prop_subcat_bytes,prop_name_bytes,prop_url_bytes,prop_data_bytes]

#DEBUG print [len(tx_ver_bytes)/2,len(tx_type_bytes)/2,len(eco_bytes)/2,len(prop_type_bytes)/2,len(prev_prop_id_bytes)/2,len(num_prop_bytes)/2,len(prop_cat_bytes)/2,len(prop_subcat_bytes)/2,len(prop_name_bytes)/2,len(prop_url_bytes)/2,len(prop_data_bytes)/2]
                                                                                                                             
#DEBUG print [byte_stream, total_bytes]

import math
total_packets = int(math.ceil(float(total_bytes)/30)) #get # of packets

total_outs = int(math.ceil(float(total_packets)/2)) #get # of outs

#construct packets
packets = []
index = 0
for i in range(total_packets):
    # 2 multisig data addrs per out, 60 bytes per, 2 characters per byte so 60 characters per pass
    parsed_data = byte_stream[index:index+60].ljust(60,"0")
    cleartext_packet =  (hex(i+1)[2:].rjust(2,"0") + parsed_data.ljust(60,"0"))

    index = index+60
    packets.append(cleartext_packet)
    #DEBUG print ['pax',cleartext_packet, parsed_data, total_packets, i]


from_address = listOptions['transaction_from']
obfuscation_packets = [hashlib.sha256(from_address).hexdigest().upper()]  #add first sha of sender to packet list
for i in range(total_packets-1): #do rest for seqnums
    obfuscation_packets.append(hashlib.sha256(obfuscation_packets[i]).hexdigest().upper())

#DEBUG print [packets,obfuscation_packets, len(obfuscation_packets[0]), len(obfuscation_packets[1]), len(packets[0])]

#obfuscate and prepare multisig outs
pair_packets = []
for i in range(total_packets):
    obfuscation_packet = obfuscation_packets[i]
    pair_packets.append((packets[i], obfuscation_packet[:-2]))

#encode the plaintext packets
obfuscated_packets = []
for pair in pair_packets:
    plaintext = pair[0].upper()
    shaaddress = pair[1] 
    #DEBUG print ['packets', plaintext, shaaddress, len(plaintext), len(shaaddress)]
    datapacket = ''
    for i in range(len(plaintext)):
        if plaintext[i] == '0':
            datapacket = datapacket + shaaddress[i]
        else:
            bin_plain = int('0x' + plaintext[i], 16)
            bin_sha = int('0x' + shaaddress[i], 16)
            #DEBUG print ['texts, plain & addr', plaintext[i], shaaddress[i],'bins, plain & addr', bin_plain, bin_sha ]
            xored = hex(bin_plain ^ bin_sha)[2:].upper()
            datapacket = datapacket + xored
    obfuscated_packets.append(( datapacket, shaaddress))

#### Test that the obfuscated packets produce the same output as the plaintext packet inputs ####

#decode the obfuscated packet
plaintext_packets = []
for pair in obfuscated_packets:
    obpacket = pair[0].upper()
    shaaddress = pair[1]
    #DEBUG print [obpacket, len(obpacket), shaaddress, len(shaaddress)]
    datapacket = ''
    for i in range(len(obpacket)):
        if obpacket[i] == shaaddress[i]:
            datapacket = datapacket + '0'
        else:
            bin_ob = int('0x' + obpacket[i], 16)
            bin_sha = int('0x' + shaaddress[i], 16)
            xored = hex(bin_ob ^ bin_sha)[2:].upper()
            datapacket = datapacket + xored
    plaintext_packets.append(datapacket)

#check the packet is formed correctly by comparing it to the input
final_packets = []
for i in range(len(plaintext_packets)):
    orig = packets[i]
    if orig.upper() != plaintext_packets[i]:
        print ['packet did not come out right', orig, plaintext_packets[i] ]
    else:
        final_packets.append(obfuscated_packets[i][0])

#DEBUG print plaintext_packets, obfuscation_packets,final_packets

#add key identifier and ecdsa byte to new mastercoin data key
for i in range(len(final_packets)):
    obfuscated = '02' + final_packets[i] + "00" 
    #DEBUG print [obfuscated, len(obfuscated)]
    invalid = True
    while invalid:
        obfuscated_randbyte = obfuscated[:-2] + hex(random.randint(0,255))[2:].rjust(2,"0").upper()
        #set the last byte to something random in case we generated an invalid pubkey
        potential_data_address = pybitcointools.pubkey_to_address(obfuscated_randbyte, magicbyte)
        if bool(conn.validateaddress(potential_data_address).isvalid):
            final_packets[i] = obfuscated_randbyte
            invalid = False
    #make sure the public key is valid using pybitcointools, if not, regenerate 
    #the last byte of the key and try again

#DEBUG print final_packets

#### Build transaction

#calculate fees
fee_total = Decimal(0.0001) + Decimal(0.000055*total_packets+0.000055*total_outs) + Decimal(0.000055)
change = largest_spendable_input['amount'] - fee_total
# calculate change : 
# (total input amount) - (broadcast fee)
if (Decimal(change) < Decimal(0) or fee_total > largest_spendable_input['amount']) and not force:
    print json.dumps({ "status": "NOT OK", "error": "Not enough funds" , "fix": "Set \'force\' flag to proceed without balance checks" })
    exit()

#retrieve raw transaction to spend it
prev_tx = conn.getrawtransaction(largest_spendable_input['txid'])

validnextinputs = []                      #get valid redeemable inputs
for output in prev_tx.vout:
    if output['scriptPubKey']['reqSigs'] == 1 and output['scriptPubKey']['type'] != 'multisig':
        for address in output['scriptPubKey']['addresses']:
            if address == listOptions['transaction_from']:
                validnextinputs.append({ "txid": prev_tx.txid, "vout": output['n']})
if testnet:
    exodus="mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv"
    moneyman="moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP"
    validnextoutputs = { exodus : 0.000055, moneyman : 0.000055 }
else:
    exodus="1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P"
    validnextoutputs = { exodus : 0.000055 }

if change > Decimal(0.000055): # send anything above dust to yourself
    validnextoutputs[ listOptions['transaction_from'] ] = float(change) 

unsigned_raw_tx = conn.createrawtransaction(validnextinputs, validnextoutputs)

json_tx =  conn.decoderawtransaction(unsigned_raw_tx)

#append  data structure
ordered_packets = []
for i in range(total_outs):
    ordered_packets.append([])

#append actual packet
index = 0
for i in range(total_outs):
    while len(ordered_packets[i]) < 2 and index != len(final_packets):
        ordered_packets[i].append(final_packets[index])
        index = index + 1
#DEBUG print ordered_packets

if len(pubkey) < 100:
    HEXSPACE_FIRST='21'
else:
    HEXSPACE_FIRST='41'

for i in range(total_outs):
    hex_string = "51" + HEXSPACE_FIRST + pubkey
    asm_string = "1 " + pubkey
    addresses = [ pybitcointools.pubkey_to_address(pubkey, magicbyte)]
    n_count = len(validnextoutputs)+i
    total_sig_count = 1
    #DEBUG print [i,'added string', ordered_packets[i]]
    for packet in ordered_packets[i]:
        hex_string = hex_string + HEXSPACE_SECOND + packet.lower() 
        asm_string = asm_string + " " + packet.lower()
        addresses.append(pybitcointools.pubkey_to_address(packet, magicbyte))
        total_sig_count = total_sig_count + 1
    hex_string = hex_string + "5" + str(total_sig_count) + "ae"
    asm_string = asm_string + " " + str(total_sig_count) + " " + "OP_CHECKMULTISIG"
    #DEBUG print [hex_string, asm_string, addresses,total_sig_count]
    #add multisig output to json object
    json_tx['vout'].append(
        { 
            "scriptPubKey": 
            { 
                "hex": hex_string, 
                "asm": asm_string, 
                "reqSigs": 1, 
                "type": "multisig", 
                "addresses": addresses 
            }, 
            "value": 0.000055*len(addresses), 
            "n": n_count
        })

#print json_tx

#construct byte arrays for transaction 
#assert to verify byte lengths are OK
version = ['01', '00', '00', '00' ]
assert len(version) == 4

num_inputs = [str(len(json_tx['vin'])).rjust(2,"0")]
assert len(num_inputs) == 1

num_outputs = [str(len(json_tx['vout'])).rjust(2,"0")]
assert len(num_outputs) == 1

sequence = ['FF', 'FF', 'FF', 'FF']
assert len(sequence) == 4

blocklocktime = ['00', '00', '00', '00']
assert len(blocklocktime) == 4

#prepare inputs data for byte packing
inputsdata = []
for _input in json_tx['vin']:
    prior_input_txhash = _input['txid'].upper()  
    prior_input_index = str(_input['vout']).rjust(2,"0").ljust(8,"0")
    input_raw_signature = _input['scriptSig']['hex']
    
    prior_txhash_bytes =  [prior_input_txhash[ start: start + 2 ] for start in range(0, len(prior_input_txhash), 2)][::-1]
    assert len(prior_txhash_bytes) == 32

    prior_txindex_bytes = [prior_input_index[ start: start + 2 ] for start in range(0, len(prior_input_index), 2)]
    assert len(prior_txindex_bytes) == 4

    len_scriptsig = ['%02x' % len(''.join([]).decode('hex').lower())] 
    assert len(len_scriptsig) == 1
    
    inputsdata.append([prior_txhash_bytes, prior_txindex_bytes, len_scriptsig])

#prepare outputs for byte packing
output_hex = []
for output in json_tx['vout']:
    value_hex = hex(int(float(output['value'])*1e8))[2:]
    value_hex = value_hex.rjust(16,"0")
    value_bytes =  [value_hex[ start: start + 2 ].upper() for start in range(0, len(value_hex), 2)][::-1]
    assert len(value_bytes) == 8
    
   # print output
    scriptpubkey_hex = output['scriptPubKey']['hex']
    scriptpubkey_bytes = [scriptpubkey_hex[start:start + 2].upper() for start in range(0, len(scriptpubkey_hex), 2)]
    len_scriptpubkey = ['%02x' % len(''.join(scriptpubkey_bytes).decode('hex').lower())]
    #assert len(scriptpubkey_bytes) == 25 or len(scriptpubkey_bytes) == 71

    output_hex.append([value_bytes, len_scriptpubkey, scriptpubkey_bytes] )

#join parts into final byte array
hex_transaction = version + num_inputs

for _input in inputsdata:
    hex_transaction += (_input[0] + _input[1] + _input[2] + sequence)

hex_transaction += num_outputs

for output in output_hex:
    hex_transaction = hex_transaction + (output[0] + output[1] + output[2]) 

hex_transaction = hex_transaction + blocklocktime

#verify that transaction is valid
assert type(conn.decoderawtransaction(''.join(hex_transaction).lower())) == type({})

#sign it
signed_transaction = conn.signrawtransaction(''.join(hex_transaction))

#output final product as JSON
#print json.dumps({ "rawtransaction": signed_transaction })
print signed_transaction['hex']

