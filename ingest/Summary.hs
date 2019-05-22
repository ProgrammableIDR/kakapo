module Summary where

import Data.Text(Text)
import qualified Data.Text as T
import Control.Arrow(second)
import Data.Map.Strict(empty,alter,insertWith,Map(),toList)
import Data.List(sortOn)
import GenParse(Samples)

summarise :: [(Text,Text)] -> [(Text,[(Text,Int)])]
summarise = map (second toList) . toList . summariseMap

    where

    summariseMap :: [(Text,Text)] -> Map Text (Map Text Int)
    summariseMap = foldl outer empty

    inner :: Text -> Map Text Int -> Map Text Int
    inner v = insertWith (+) v 1

    outer :: Map Text (Map Text Int) -> (Text,Text) -> Map Text (Map Text Int)
    outer m ( k, v) = alter (f v) k m
        where f :: Text -> Maybe (Map Text Int) -> Maybe (Map Text Int)
              f v Nothing = Just $ inner v empty
              f v (Just m) = Just $ inner v m


analyse2 :: Samples -> IO ()
analyse2 samples = do
    let hdrs = Summary.summarise $ concatMap fst samples
        count = length samples
    putStrLn $ show count ++ " samples found"
    let nlabels = length hdrs
    putStrLn $ show nlabels ++ " metadata labels found"
    let pInvariant = (1 ==) . length . snd
        pUnassociated = ((count `div` 2) < ) . length . snd
        pVariable x = not ( pInvariant x) && not ( pUnassociated x)
        invariants = filter pInvariant hdrs
        unassociated = filter pUnassociated hdrs
        variable = filter pVariable hdrs
    putStrLn $ show (length invariants) ++ " invariants found: " ++ unwords ( map ( T.unpack . fst )  invariants)
    putStrLn $ show (length unassociated) ++ " unassociated found: " ++ unwords ( map ( T.unpack . fst) unassociated)
    putStrLn $ show (length variable) ++ " variable found: " ++ unwords ( map ( T.unpack . fst) variable)

analyse :: Samples -> IO () 
analyse samples = do
   let hdrs = Summary.summarise $ concatMap fst samples
       sortedHeaders = map (second (reverse . sortOn snd)) $ sortOn fst hdrs
   mapM_ (putStrLn . display) sortedHeaders

   where
       display (k,vs) = T.unpack k ++ " : " ++ show (length vs) ++ " { " ++ unwords ( map show' vs) ++ " }"
       show' (t,i) = T.unpack t ++ "[" ++ show i ++ "]"
