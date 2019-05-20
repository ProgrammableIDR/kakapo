{-# LANGUAGE OverloadedStrings #-}
module Constraints where
import Control.Monad(when)
import Data.Attoparsec.Text
import Data.Text(Text)
import qualified Data.Text as T
import Data.List(elem)
import Data.Maybe(isNothing)
import Control.Applicative((<|>))
import Data.Either(fromRight, partitionEithers)
import qualified Data.Map.Strict as Map
import System.Exit(die)
import GenParse(Metrics,Dict,Sample)

prove = do
    print $ getConstraint "MATCH=B"
    print $ getConstraint "RANGE=1-9"
    print $ getConstraint "INDEX=you,me,both"
    print $ getConstraint "EMPTYINDEX=,"
    print $ getConstraint "WILDCARD=*"
    print $ getConstraint "CONTROL=?"
    putStrLn "Done"

data Constraint = Control | Any | Equality Text | Range Int Int | Index [ Text ] deriving ( Eq, Show )

isControl Control = True
isControl _ = False

isIndex ( Index _ ) = True
isIndex _ = False

type Selector =  Map.Map Text Constraint
-- should be oblivious to the type of Metrics
type SelectResult = Map.Map Text [(Text,Sample)]

-- conttsraint application: transform an accumulator depending on the found constraint and given value
-- logic: get the constraint (or Nothing)
-- by case apply the constraint to the acc
-- the acc is simply a Maybe tuple of Maybe Text, set initially to Just (Nothing,Nothing)
-- match failure returns Nothing
-- Control and Index matches update fst or snd respectively with Just.

emptySelectResult :: SelectResult
emptySelectResult = Map.empty

select :: Selector -> [Sample] -> SelectResult
select selector = foldl (inner selector) emptySelectResult

inner :: Selector -> SelectResult -> Sample -> SelectResult
inner selector base sample@(header,content) = let
    indexes = filter isIndex $ Map.elems selector
    indexNotRequested = null indexes
    openIndexRequested = not indexNotRequested && head indexes == Index []
    Index enumeratedIndexes = head indexes
    acc0 = Just (Nothing,Nothing)
    accFinal = foldl (\acc (k,v) -> f (Map.lookup k selector) acc v) acc0 header

    --f Nothing                   _                  _ = Nothing -- lookup failed -> unexpected header parameter
    f Nothing                   x                  _ = x -- ignore lookup failed
    f _                         Nothing            _ = Nothing -- propagate an earlier failure in the chain
    f ( Just Any )              x                  _ = x
    f ( Just Control )          (Just (Nothing,x)) v = Just (Just v,x)
    f ( Just (Index _) )        (Just (x,Nothing)) v = Just (x,Just v)
    f ( Just (Equality t) )     x                  v = if t == v then x else Nothing
    f ( Just (Range low high) ) x                  v = if read ( T.unpack v) < low || read ( T.unpack v) > high then Nothing else x

    g a (Just ax) = Just ( a:ax )
    g a Nothing = Just [a]
    in case accFinal of
        Nothing                         -> base
        Just (Nothing,_)                -> base
        Just (Just control, Nothing)    -> if indexNotRequested then Map.alter (g (control,sample)) "" base else base
        Just (Just control, Just index) -> if openIndexRequested || elem index enumeratedIndexes then Map.alter (g (control,sample)) index base else base


type RawConstraint = Either String (Text, Constraint)

getConstraint :: String -> RawConstraint
getConstraint s = parseOnly parseConstraint $ T.pack s

parseConstraint :: Parser (Text,Constraint)
parseConstraint = do
    key <- takeTill1 ('='==)
    char '='
    pred <- control <|> wildcard <|> single <|> range <|> emptyIndex <|> index
    return (key,pred)

control = do
    char '?'
    requireEOT
    return Control

wildcard = do
    char '*'
    requireEOT
    return Any

single = do
    match <- Data.Attoparsec.Text.takeWhile (notInClass "-,")
    requireEOT
    return $ Equality match

range = do
    low <- decimal
    char '-'
    high <- decimal
    requireEOT
    return $ Range low high

emptyIndex = do
    char ','
    requireEOT
    return $ Index []

index = do
    vx <- takeTill1 (','==) `sepBy1` char ','
    requireEOT
    return $ Index vx

takeTill1 p = takeWhile1 (not . p)
requireEOT :: Parser ()
requireEOT = do
    m <- peekChar
    if isNothing m then return() else fail "eot"

--newtype TSample a = TSample Text a
--type Sample = TSample Metrics

buildSelector :: [ RawConstraint ] -> IO Selector
buildSelector rawConstraints = do
    let (fails,constraints) = partitionEithers rawConstraints
    if
        not (null fails)
    then do
        putStrLn "Couldn't read selection constraints:"
        mapM_ putStrLn fails
        die ""
    else do
        when ( 1 /= length ( filter ( isControl . snd ) constraints))
             (die "exactly one control parameter required")
        when ( 1 < length ( filter ( isIndex . snd ) constraints))
             (die "at most one index parameter required")
        return $ Map.fromList constraints
