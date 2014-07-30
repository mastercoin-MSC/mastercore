#Assumes the following 
#Running build: https://github.com/faizkhan00/mastercore/tree/regtest-michael-0921

echo -e "\nUSAGE: \n-rm, -s{1,2,3}, -h: removes all regtest blocks/wallets, starts node 1,2 or 3, this help"

if [[ "$1" = "-rm" ]]
then
  rm ~/.bitcoin/regtest/ -rf
  rm ~/.bitcoin2/regtest/ -rf
  rm ~/.bitcoin3/regtest/ -rf
fi

if [[ "$1" = "-s1" ]]
then
  echo "Starting node 1..."; mkdir ~/.bitcoin/ >> /dev/null

  cpulimit -l 60 ./bitcoind -txindex -printtoconsole -regtest -server -rpcport=8330 -rpcssl=0 -datadir=/home/faiz/.bitcoin/ -port=18333 -rpcuser=user -rpcpassword=password 

fi

if [[ "$1" = "-s2" ]]
then
  echo "Starting node 2..."; mkdir ~/.bitcoin2/ >> /dev/null

  cpulimit -l 60 ./bitcoind -txindex -printtoconsole -regtest -server -rpcport=8332 -rpcssl=0 -datadir=/home/faiz/.bitcoin2/ -port=18444 -rpcuser=user -rpcpassword=password 
fi

if [[ "$1" = "-s3" ]]
then
  echo "Starting node 3..."; mkdir ~/.bitcoin3/ >> /dev/null

  cpulimit -l 60 ./bitcoind -txindex -printtoconsole -regtest -server -rpcport=8334 -rpcssl=0 -datadir=/home/faiz/.bitcoin3/ -port=18555 -rpcuser=user -rpcpassword=password 
fi

if [[ "$1" = "-conn2" ]]
then
  echo "Connecting 2 nodes..."
  ./bitcoin-cli -rpcport=8330 -rpcuser=user -rpcpassword=password addnode localhost:18444 onetry        
  ./bitcoin-cli -rpcport=8332 -rpcuser=user -rpcpassword=password addnode localhost:18333 onetry  
  echo "Nodes 1 and 2 are now connected. Close them to disconnect."      
fi

if [[ "$1" = "-conn3" ]]
then
  echo "Connecting 3 nodes..."
  ./bitcoin-cli -rpcport=8330 -rpcuser=user -rpcpassword=password addnode localhost:18444 onetry        
  ./bitcoin-cli -rpcport=8332 -rpcuser=user -rpcpassword=password addnode localhost:18333 onetry  
  ./bitcoin-cli -rpcport=8334 -rpcuser=user -rpcpassword=password addnode localhost:18444 onetry  
  echo "Nodes 1 and 2 and 3 are now connected. Close them to disconnect."      
fi
