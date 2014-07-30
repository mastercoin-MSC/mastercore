#Assumes the following 
#Running build: https://github.com/faizkhan00/mastercore/tree/regtest-michael-0921

echo -e "\nUSAGE: \n-g{1,2,3}, -h: generates 101 blocks,50 BTC, and 50MSC, this help"

callnode1() {
  ./bitcoin-cli -rpcport=8330 -rpcuser=user -rpcpassword=password $@
}

callnode2() {
  ./bitcoin-cli -rpcport=8332 -rpcuser=user -rpcpassword=password $@
}

callnode3() {
  ./bitcoin-cli -rpcport=8334 -rpcuser=user -rpcpassword=password $@
}
if [[ "$1" = "-g1" ]]
then
  echo "Generating 101 blocks on node 1...";
   ADDR1=$(callnode1 getnewaddress "node1") 
   callnode1 setgenerate true 101 
   callnode1 sendtoaddress "moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP" 0.5
   callnode1 sendtoaddress "mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv" 0.5
   callnode1 setgenerate true  
   callnode1 getallbalancesforid_MP 1
fi

if [[ "$1" = "-g2" ]]
then
  echo "Generating 101 blocks on node 2...";
   ADDR1=$(callnode2 getnewaddress "node2") 
   callnode2 setgenerate true 101 
   callnode2 sendtoaddress "moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP" 0.5
   callnode2 sendtoaddress "mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv" 0.5
   callnode2 setgenerate true  
   callnode2 getallbalancesforid_MP 1
fi

if [[ "$1" = "-g3" ]]
then
  echo "Generating 101 blocks on node 3...";

   ADDR1=$(callnode3 getnewaddress "node3") 
   callnode3 setgenerate true 101 
   callnode3 sendtoaddress "moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP" 0.5
   callnode3 sendtoaddress "mpexoDuSkGGqvqrkrjiFng38QPkJQVFyqv" 0.5
   callnode3 setgenerate true  
   callnode3 getallbalancesforid_MP 1
fi

