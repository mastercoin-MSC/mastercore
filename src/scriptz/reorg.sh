#Assumes the following 
#Running build: https://github.com/faizkhan00/mastercore/tree/regtest-michael-0921

echo -e "\nUSAGE: \n starts the test case for a re-org"

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

   ADDR1=$(callnode1 getnewaddress)
   echo -e "Made new address $ADDR1 ...\n"
   callnode1 setaccount $ADDR1 node1 
   
   ADDR2=$(callnode1 getnewaddress)
   echo -e "Made new address $ADDR2 ...\n"
   callnode1 setaccount $ADDR2 node1-friendly

   echo -e "Generating new blocks... \n" 
   callnode1 setgenerate true 101 
   sleep 5
   
   echo -e "Attempting send to address $ADDR1 and $ADDR2 ...\n"
   callnode1 sendtoaddress $ADDR1 5.0
   callnode1 sendtoaddress $ADDR2 5.0

   echo -e "Committing transaction...\n"
   callnode1 setgenerate true 3
   sleep 5

   echo -e "Sending to get MSC...\n"
   callnode1 sendmany node1 "{\"moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP\":0.5,\"mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv\":0.5}"

   callnode1 sendmany node1-friendly "{\"moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP\":0.5,\"mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv\":0.5}"

   echo -e "Committing transaction...\n" 
   callnode1 setgenerate true 3
   sleep 5

   echo -e "Attempting send to address $ADDR1 and $ADDR2 ...\n"
   callnode1 sendtoaddress $ADDR1 5.0
   callnode1 sendtoaddress $ADDR2 5.0

   echo -e "Committing transaction...\n" 
   callnode1 setgenerate true 3
   sleep 5

   echo -e "Sending money from $ADDR1 to $ADDR2\n"
   callnode1 send_MP $ADDR1 $ADDR2 1 15.0
   callnode1 send_MP $ADDR2 $ADDR1 1 2.8
   callnode1 send_MP $ADDR1 $ADDR2 1 12.8

   echo -e "Committing transactions...\n"
   callnode1 setgenerate true 3
   sleep 5
 
   echo -e "Showing results..."
   callnode1 getallbalancesforid_MP 1

   echo -e "Done. \n\n"
   sleep 10

   echo "Starting node 2 setup...";

   ADDR1_=$(callnode2 getnewaddress)
   echo -e "Made new address $ADDR1_ ...\n"
   callnode2 setaccount $ADDR1_ node2 
   
   ADDR2_=$(callnode2 getnewaddress)
   echo -e "Made new address $ADDR2_ ...\n"
   callnode2 setaccount $ADDR2_ node2-friendly

   echo -e "Generating new blocks... \n" 
   callnode2 setgenerate true 101 
   sleep 5
   
   echo -e "Attempting send to address $ADDR1_ and $ADDR2_ ...\n"
   callnode2 sendtoaddress $ADDR1_ 5.0
   callnode2 sendtoaddress $ADDR2_ 5.0

   echo -e "Committing transaction...\n"
   callnode2 setgenerate true 3
   sleep 5

   echo -e "Sending to get MSC...\n"
   callnode2 sendmany node2 "{\"moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP\":0.5,\"mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv\":0.5}"

   callnode2 sendmany node2-friendly "{\"moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP\":0.5,\"mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv\":0.5}"

   echo -e "Committing transaction...\n" 
   callnode2 setgenerate true 3
   sleep 5

   echo -e "Attempting send to address $ADDR1_ and $ADDR2_ ...\n"
   callnode2 sendtoaddress $ADDR1_ 5.0
   callnode2 sendtoaddress $ADDR2_ 5.0

   echo -e "Committing transaction...\n" 
   callnode2 setgenerate true 3
   sleep 5

   #echo -e "Sending money from $ADDR1_ to $ADDR2_\n"
   #callnode2 send_MP $ADDR1_ $ADDR2_ 1 12.0
   #callnode2 send_MP $ADDR2_ $ADDR1_ 1 2.81
   #callnode2 send_MP $ADDR1_ $ADDR2_ 1 1.8

   #echo -e "Committing transactions...\n"
   #callnode2 setgenerate true 3
   #sleep 5
 
   echo -e "Showing results..."
   callnode2 getallbalancesforid_MP 1

   echo -e "Done. \n\n"
   sleep 10

   echo "Starting node 3 setup...";

   ADDR1__=$(callnode3 getnewaddress)
   echo -e "Made new address $ADDR1__ ...\n"
   callnode3 setaccount $ADDR1__ node3 
   
   ADDR2__=$(callnode3 getnewaddress)
   echo -e "Made new address $ADDR2__ ...\n"
   callnode3 setaccount $ADDR2__ node3-friendly

   echo -e "Generating new blocks... \n" 
   callnode3 setgenerate true 101 
   sleep 5
   
   echo -e "Attempting send to address $ADDR1__ and $ADDR2__ ...\n"
   callnode3 sendtoaddress $ADDR1__ 5.0
   callnode3 sendtoaddress $ADDR2__ 5.0

   echo -e "Committing transaction...\n"
   callnode3 setgenerate true 3
   sleep 5

   echo -e "Sending to get MSC...\n"
   callnode3 sendmany node3 "{\"moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP\":0.5,\"mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv\":0.5}"

   callnode3 sendmany node3-friendly "{\"moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP\":0.5,\"mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv\":0.5}"

   echo -e "Committing transaction...\n" 
   callnode3 setgenerate true 3
   sleep 5

   echo -e "Attempting send to address $ADDR1__ and $ADDR2__ ...\n"
   callnode3 sendtoaddress $ADDR1__ 5.0
   callnode3 sendtoaddress $ADDR2__ 5.0

   echo -e "Committing transaction...\n" 
   callnode3 setgenerate true 3
   sleep 5

   echo -e "Showing results..."
   callnode3 getallbalancesforid_MP 1

   echo -e "Done. \n\n"
   sleep 10

   ####################################################

   echo "Connecting 2 nodes..."
   #callnode1 addnode localhost:18444 onetry        
   callnode2 addnode localhost:18333 onetry
   echo "Nodes 1 and 2 are now connected. Close them to disconnect."      

   #sync nodes 1 and 2
   callnode1 setgenerate true 15
   BHASH=$(callnode1 getbestblockhash)
   BHASH__=$(callnode2 getbestblockhash)

  echo -e "\n\nBlock hashes should now be equal:\n "$BHASH" and "$BHASH__" "

  # now make node 3 the longest (reorg soon)

  callnode3 setgenerate true 5
  callnode3 sendfrom "node3" "$ADDR2__" 0.5
  callnode3 sendfrom "node3" "$ADDR2__" 0.5
  callnode3 sendfrom "node3" "$ADDR2__" 0.5
  callnode3 sendfrom "node3" "$ADDR2__" 0.5
  callnode3 setgenerate true 5
  callnode3 sendfrom "node3" "$ADDR2__" 0.5
  callnode3 sendfrom "node3" "$ADDR2__" 0.5
  callnode3 sendfrom "node3" "$ADDR2__" 0.5
  callnode3 sendfrom "node3" "$ADDR2__" 0.5
  callnode3 setgenerate true 10
  
  echo "Connecting 3 nodes..."
  callnode3 addnode localhost:18444 onetry 
  echo "Nodes 1 and 2 and 3 are now connected. Close them to disconnect."     

  #sync nodes 1 and 2 with 3 (lose MP txes on 1 and 2) 

  callnode3 setgenerate true 10

  callnode1 getallbalancesforid_MP 1
  callnode2 getallbalancesforid_MP 1
  callnode3 getallbalancesforid_MP 1
