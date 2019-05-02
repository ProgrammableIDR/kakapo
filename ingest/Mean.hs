module Mean where
import qualified Data.Text as T
import Data.List(sort)

type Point = (Int,Double,Double,Double)
--tuple42_2 (_,x,_,_) = x
--tuple42_24 (_,x,_,y) = (x,y)

mean :: [Double] -> Double
mean = (\(_,x,_,_) -> x) . meanRSD

meanRSD :: [Double] -> (Int,Double,Double,Double)
meanRSD sample = 
        let
           count = fromIntegral $ length sample
           ssum  = sum sample
           mean  = ssum / count
           sqSum = foldl (\x y -> x + y * y) 0 sample
           sd    = sqrt ( (count * sqSum) - (ssum * ssum) ) / count
           rsd   = sd / mean 
        in (length sample, mean, sd, rsd)

--least :: [Double] -> Double
--least sample = minimum sample

sndLeast :: [Double] -> Double
sndLeast sample | 1 < length sample = (sort sample) !! 1
                | otherwise =  minimum sample