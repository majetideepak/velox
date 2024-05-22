* (optional) Install Docker desktop for Mac users who do not have Velox dependencies installed

 `brew install --cask docker`

* Pull the Velox docker image which contains the dependencies and create a container
```
 docker pull ghcr.io/facebookincubator/velox-dev:centos8
 docker run -it ghcr.io/facebookincubator/velox-dev:centos8
```
* Build VeloxMiniApp
```
 git clone https://github.com/majetideepak/velox
 cd velox && git checkout veloxminiapp
 make veloxminiapp
```