#Assumes the following 
#Running build: https://github.com/faizkhan00/mastercore/tree/regtest-michael-0921

echo -e "\nUSAGE: \nStarts the test case for a re-org\n"

callnode1() {
  ./bitcoin-cli -rpcport=8330 -rpcuser=user -rpcpassword=password $@
}

callnode2() {
  ./bitcoin-cli -rpcport=8332 -rpcuser=user -rpcpassword=password $@
}

callnode3() {
  ./bitcoin-cli -rpcport=8334 -rpcuser=user -rpcpassword=password $@
}
   #rm before starting this
   #rm ~/.bitcoin/regtest/ -rf
   #rm ~/.bitcoin2/regtest/ -rf
   #rm ~/.bitcoin3/regtest/ -rf

   connect_nodes() {
     echo "Connecting nodes..."
     callnode3 addnode localhost:18333 onetry 
     echo "Nodes 1 and 3 are now connected."
   }

   init_node1() {
     echo -e "Generating new blocks..." 
     callnode1 setgenerate true 102 
     sleep 5
   }

   connect_nodes
   
   init_node1

   ADDR1=$(callnode1 getnewaddress)
   echo -e "Made new address $ADDR1 ..."
   callnode1 setaccount $ADDR1 node1 
   
   ADDR2=$(callnode1 getnewaddress)
   echo -e "Made new address $ADDR2 ..."
   callnode1 setaccount $ADDR2 node1-friendly

   echo -e "Attempting send to address $ADDR1 and $ADDR2 ..."
   callnode1 sendtoaddress $ADDR1 2.0
   callnode1 sendtoaddress $ADDR2 2.0

   echo -e "Committing transaction..."
   callnode1 setgenerate true 1
   sleep 1

   echo -e "Sending to get MSC...\n"
   callnode1 sendmany node1 "{\"moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP\":0.5,\"mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv\":0.5}"

   callnode1 sendmany node1-friendly "{\"moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP\":0.5,\"mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv\":0.5}"

   echo -e "Committing transaction...\n" 
   callnode1 setgenerate true 1
   sleep 1

   #echo -e "Sending to get MSC...\n"
   #callnode1 sendmany node1 "{\"moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP\":0.5,\"mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv\":0.5}"

   #callnode1 sendmany node1-friendly "{\"moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP\":0.5,\"mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv\":0.5}"

   #echo -e "Committing transaction...\n" 
   #callnode1 setgenerate true 1
   #sleep 1

   #so the to-be-orphaned transactions are not shared 
   #callnode1 getinfo
   #callnode3 getinfo
   #sleep 5

   echo -e "Stopping node 3 temporarily..."
   callnode3 stop 

   echo -e "Attempting send to address $ADDR1 and $ADDR2 ...\n"
   callnode1 sendtoaddress $ADDR1 1.0
   callnode1 sendtoaddress $ADDR2 1.0

   echo -e "Committing transaction...\n" 
   callnode1 setgenerate true 1
   sleep 1

   echo -e "Sending MSC from $ADDR1 to $ADDR2\n"
   TX1=$(callnode1 send_MP $ADDR1 $ADDR2 1 1.0)
   TX2=$(callnode1 send_MP $ADDR2 $ADDR1 1 2.8)
   TX3=$(callnode1 send_MP $ADDR1 $ADDR2 1 3.25)

   echo -e "Committing transactions...\n"
   callnode1 setgenerate true 1
   sleep 5
 
   echo -e "Showing results..."
   callnode1 getallbalancesforid_MP 1

   echo -e "$TX1 raw..."
   callnode1 getrawtransaction $TX1

   echo -e "$TX2 raw..."
   callnode1 getrawtransaction $TX2
   
   echo -e "$TX3 raw..."
   callnode1 getrawtransaction $TX3

   echo -e "Done. Now we will attempt a reorg. \n\n"
   sleep 1

   echo -e "Start node 3..."
   sleep 15
   #Node 3 started up in isolation
   callnode3 setgenerate true 20

   echo "Connecting nodes..."
   callnode3 addnode localhost:18333 onetry 
   echo "Nodes 1 and 3 are now connected."
   sleep 15

   BHASH=$(callnode1 getbestblockhash)
   BHASH__=$(callnode3 getbestblockhash)
   echo -e "\n\nBlock hashes should now be equal:\n "$BHASH" and "$BHASH__" "
   
   echo -e "Compare working state (balances) Node 1..."
   callnode1 getallbalancesforid_MP 1
   echo -e "Compare working state (balances) Node 3..."
   callnode3 getallbalancesforid_MP 1
   
   echo -e "Looking for $TX1"
   callnode1 getrawtransaction $TX1
   callnode3 getrawtransaction $TX1

   echo -e "Looking for $TX2"
   callnode1 getrawtransaction $TX2
   callnode3 getrawtransaction $TX2
   
   echo -e "Looking for $TX3"
   callnode1 getrawtransaction $TX3
   callnode3 getrawtransaction $TX3
   
   echo -e "Feel free to stop here and inspect, old transactions in raw above are not part of the longest chain"
   sleep 30

   callnode1 stop

   echo -e "Start node 1..."
   sleep 15

   callnode1 getallbalancesforid_MP 1 #old balances are back

   #done
   callnode1 stop
   callnode2 stop
